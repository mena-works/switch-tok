#include "net.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <sys/socket.h>
#include <unistd.h>

#include <borealis.hpp>
#include <curl/curl.h>
#include <switch.h>

#include "dns.hpp"

namespace
{

struct Sink
{
    std::vector<uint8_t>* out;
    size_t                limit;  // 0 = unlimited
};

size_t writeToVector(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto*        sink  = static_cast<Sink*>(userdata);
    const size_t bytes = size * nmemb;

    // Enforced here rather than after the fact: a chunked response advertises
    // no length, so CURLOPT_MAXFILESIZE never fires and the transfer runs to
    // completion before anyone checks. One such clip pulled 136 MB over the
    // console's wifi and starved the player's own connection.
    if (sink->limit && sink->out->size() + bytes > sink->limit)
        return 0;  // aborts the transfer

    sink->out->insert(sink->out->end(), ptr, ptr + bytes);
    return bytes;
}

// curl connects non-blocking and expects EINPROGRESS while that is pending.
// Citron answers EAGAIN instead, which curl reads as a hard failure, so every
// request dies on a connection that would have worked -- measured directly: our
// own blocking connect to the same address succeeds moments before curl's fails.
//
// So hand curl a socket that is already connected and tell it to skip its own
// connect. Only used on the fallback path, so a console never sees it.
curl_socket_t openConnectedSocket(void*, curlsocktype, struct curl_sockaddr* address)
{
    const int fd = socket(address->family, address->socktype, address->protocol);
    if (fd < 0)
        return CURL_SOCKET_BAD;

    if (connect(fd, &address->addr, address->addrlen) != 0)
    {
        close(fd);
        return CURL_SOCKET_BAD;
    }

    return static_cast<curl_socket_t>(fd);
}

int markAlreadyConnected(void*, curl_socket_t, curlsocktype)
{
    return CURL_SOCKOPT_ALREADY_CONNECTED;
}

bool splitHost(const std::string& url, std::string& host, std::string& port);

static std::string g_sessionId;

// The sessionid is a TikTok account credential. It must only ever be attached
// to a request that actually goes to TikTok -- never to the tikwm.com mirror or
// any other third party, which do not use it and would simply be handed a live
// session cookie. Gate every cookie site on this rather than trusting callers.
bool isTikTokHost(const std::string& url)
{
    std::string host, port;
    if (!splitHost(url, host, port))
        return false;

    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto endsWith = [&](const char* suffix) {
        const std::string s = suffix;
        return host.size() >= s.size() &&
               host.compare(host.size() - s.size(), s.size(), s) == 0;
    };

    return endsWith(".tiktok.com") || host == "tiktok.com" ||
           endsWith(".tiktokv.com") || endsWith(".byteoversea.com");
}



// Undoes Transfer-Encoding: chunked.
//
// Asking for Connection: close does not stop a server from chunking, and this
// one does. The chunk sizes then sit in the body as stray hex lines, which
// surfaces as "parse error at line 2, column 1" from the JSON parser -- a
// message that says nothing about the real cause.
bool dechunk(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    size_t at = 0;

    while (at < in.size())
    {
        size_t lineEnd = at;
        while (lineEnd + 1 < in.size() && !(in[lineEnd] == '\r' && in[lineEnd + 1] == '\n'))
            ++lineEnd;

        if (lineEnd + 1 >= in.size())
            return false;

        const std::string sizeLine(in.begin() + at, in.begin() + lineEnd);
        const long        size = std::strtol(sizeLine.c_str(), nullptr, 16);

        at = lineEnd + 2;

        if (size <= 0)
            return true;  // terminating chunk

        if (at + static_cast<size_t>(size) > in.size())
            return false;

        out.insert(out.end(), in.begin() + at, in.begin() + at + size);
        at += static_cast<size_t>(size) + 2;  // skip the chunk's trailing CRLF
    }

    return true;
}

// Drives the transfer by hand over a curl-established TLS connection.
//
// curl waits on poll() before every read. Citron's poll never reports the
// socket readable, so curl only retries recv when its own internal timers fire
// -- measured at 79 bytes in 60 seconds, while recv itself returns data fine.
// Connecting with CONNECT_ONLY and then reading in a tight loop skips poll
// entirely.
//
// Only correct because the request is a plain GET and we ask the server to
// close when done, so "read until EOF" is the whole framing rule. Do not reuse
// this for anything needing keep-alive or chunked bodies.
bool fetchWithoutPoll(CURL* curl, const std::string& url, std::vector<uint8_t>& out,
                      long timeoutSeconds, long& status)
{
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);

    if (curl_easy_perform(curl) != CURLE_OK)
        return false;

    std::string host, port;
    if (!splitHost(url, host, port))
        return false;

    std::string path = "/";
    const size_t schemeEnd = url.find("://");
    const size_t pathStart = url.find('/', schemeEnd + 3);
    if (pathStart != std::string::npos)
        path = url.substr(pathStart);

    std::string request = "GET " + path +
                                " HTTP/1.1\r\n"
                                "Host: " + host + "\r\n"
                                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 "
                                "Safari/537.36\r\n"
                                "Accept: */*\r\n"
                                "Accept-Encoding: identity\r\n"
                                "Connection: close\r\n";

    if (!g_sessionId.empty() && isTikTokHost(url))
        request += "Cookie: sessionid=" + g_sessionId + "\r\n";

    request += "\r\n";

    size_t sent = 0;
    while (sent < request.size())
    {
        size_t   n  = 0;
        CURLcode rc = curl_easy_send(curl, request.data() + sent, request.size() - sent, &n);
        if (rc == CURLE_OK)
            sent += n;
        else if (rc != CURLE_AGAIN)
            return false;
    }

    std::vector<uint8_t> raw;
    const time_t         deadline = time(nullptr) + timeoutSeconds;

    for (;;)
    {
        char     buffer[16384];
        size_t   got = 0;
        CURLcode rc  = curl_easy_recv(curl, buffer, sizeof(buffer), &got);

        if (rc == CURLE_OK)
        {
            if (got == 0)
                break;  // server closed: the body is complete
            raw.insert(raw.end(), buffer, buffer + got);
        }
        else if (rc != CURLE_AGAIN)
        {
            break;
        }

        if (time(nullptr) > deadline)
            return false;
    }

    // Split off the status line and headers; everything after is the body.
    static const std::string marker = "\r\n\r\n";
    const auto               it     = std::search(raw.begin(), raw.end(), marker.begin(), marker.end());
    if (it == raw.end())
        return false;

    const std::string head(raw.begin(), it);
    if (head.compare(0, 5, "HTTP/") == 0)
    {
        const size_t space = head.find(' ');
        if (space != std::string::npos)
            status = std::strtol(head.c_str() + space + 1, nullptr, 10);
    }

    std::string lowered = head;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::vector<uint8_t> body(it + marker.size(), raw.end());

    if (lowered.find("transfer-encoding: chunked") != std::string::npos)
    {
        out.clear();
        if (!dechunk(body, out))
            return false;
    }
    else
    {
        out = body;
    }

    return true;
}

// "https://host:port/path" -> host, and the port curl will use.
bool splitHost(const std::string& url, std::string& host, std::string& port)
{
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return false;

    port = url.compare(0, 5, "https") == 0 ? "443" : "80";

    const size_t start = scheme + 3;
    size_t       end   = url.size();
    for (size_t i = start; i < url.size(); ++i)
    {
        const char c = url[i];
        if (c == '/' || c == '?' || c == '#')
        {
            end = i;
            break;
        }
        if (c == ':')
        {
            end = i;
            size_t portEnd = url.find_first_of("/?#", i + 1);
            if (portEnd == std::string::npos)
                portEnd = url.size();
            port = url.substr(i + 1, portEnd - i - 1);
            break;
        }
    }

    host = url.substr(start, end - start);
    return !host.empty();
}

// devkitPro's curl links against mbedtls, which has no system trust store.
// Drop curl's cacert.pem into romfs/ (see README) or stay on plain http on LAN.
constexpr const char* kCaBundle = "romfs:/cacert.pem";

// curl reports "Timeout was reached" for a stalled DNS lookup, a stalled TCP
// connect and a stalled TLS handshake alike, so route its running commentary
// into the log to tell those apart.
int debugCallback(CURL*, curl_infotype type, char* data, size_t size, void*)
{
    if (type != CURLINFO_TEXT)
        return 0;

    std::string line(data, size);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();

    if (!line.empty())
        brls::Logger::debug("curl: {}", line);

    return 0;
}

} // namespace

namespace net
{

void loadSessionId()
{
    const char* paths[] = {
        "sdmc:/switch/switch-tok.sessionid",
        "sdmc:/switch-tok.sessionid",
        ".sessionid"
    };

    for (const char* path : paths)
    {
        if (FILE* file = fopen(path, "rb"))
        {
            char buffer[256] = {0};
            size_t got = fread(buffer, 1, sizeof(buffer) - 1, file);
            fclose(file);

            std::string content(buffer, got);
            content.erase(std::remove_if(content.begin(), content.end(), ::isspace), content.end());

            if (!content.empty())
            {
                g_sessionId = content;
                brls::Logger::info("sessionid loaded from {}", path);
                return;
            }
        }
    }
}

void globalInit()
{
    loadSessionId();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // mbedtls fails closed if the bundle is unreadable, and a romfs that did
    // not mount is easy to mistake for a network problem.
    if (FILE* bundle = fopen(kCaBundle, "rb"))
    {
        fseek(bundle, 0, SEEK_END);
        brls::Logger::info("CA bundle ok: {} bytes", ftell(bundle));
        fclose(bundle);
    }
    else
    {
        brls::Logger::error("CA bundle missing at {}", kCaBundle);
    }

    // Applet mode gets 2 BSD sessions, title takeover gets 12. curl stalling
    // on connect is a normal symptom of running out of them.
    brls::Logger::info("applet type: {}", static_cast<int>(appletGetAppletType()));
}

void globalExit()
{
    curl_global_cleanup();
}

bool get(const std::string& url, std::vector<uint8_t>& out, long timeoutSeconds, long maxBytes)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    out.clear();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    Sink sink { &out, maxBytes > 0 ? static_cast<size_t>(maxBytes) : 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToVector);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundle);
    
    if (!g_sessionId.empty() && isTikTokHost(url))
    {
        // CURLOPT_COOKIE copies the string, so a local is fine.
        const std::string cookieHeader = "sessionid=" + g_sessionId;
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookieHeader.c_str());
    }

    // The feed works with any User-Agent -- an early timeout here turned out to
    // be transient, not a rejection. A browser string is kept anyway because the
    // endpoint sits behind Cloudflare, which is friendlier to one.
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // gzip: the feed is ~70 KB of JSON

    if (maxBytes > 0)
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maxBytes));

    CURLcode result = curl_easy_perform(curl);

    // Emulators implement BSD sockets but not sfdnsres, so getaddrinfo fails
    // while the network itself is fine. Resolve it ourselves over UDP and hand
    // curl the answer. A console never reaches this branch.
    curl_slist* resolved = nullptr;
    if (result == CURLE_COULDNT_RESOLVE_HOST)
    {
        std::string host, port, ip;
        if (splitHost(url, host, port) && dns::resolveA(host, ip))
        {
            curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, openConnectedSocket);
            curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, markAlreadyConnected);

            // Emulated sockets move a fraction of the console's throughput: a
            // 56 KB feed arrived at 13 KB in the first 20 seconds. The console
            // keeps the short timeout, because there a stall is a real failure
            // and waiting three times as long only delays saying so.
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds * 3);

            // Kept on for this path. It is the only branch that still fails
            // unpredictably, and debugging it blind cost more than the noise.
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debugCallback);

            const std::string entry = host + ":" + port + ":" + ip;
            resolved                = curl_slist_append(nullptr, entry.c_str());
            curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);

            out.clear();

            long manualStatus = 0;
            if (fetchWithoutPoll(curl, url, out, timeoutSeconds * 3, manualStatus))
            {
                brls::Logger::info("net: {} fetched without poll, {} bytes, HTTP {}", host,
                                   out.size(), manualStatus);

                if (resolved)
                    curl_slist_free_all(resolved);
                curl_easy_cleanup(curl);
                return manualStatus < 400;
            }

            result = curl_easy_perform(curl);
        }
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (resolved)
        curl_slist_free_all(resolved);

    if (result != CURLE_OK)
    {
        brls::Logger::error("GET {} failed: {}", url, curl_easy_strerror(result));
        return false;
    }

    if (status >= 400)
    {
        brls::Logger::error("GET {} returned HTTP {}", url, status);
        return false;
    }

    return true;
}

std::string urlEncode(const std::string& value)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return value;

    std::string encoded = value;
    if (char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size())))
    {
        encoded = escaped;
        curl_free(escaped);
    }

    curl_easy_cleanup(curl);
    return encoded;
}

bool getString(const std::string& url, std::string& out, long timeoutSeconds)
{
    std::vector<uint8_t> bytes;
    if (!get(url, bytes, timeoutSeconds))
        return false;

    out.assign(bytes.begin(), bytes.end());
    return true;
}

} // namespace net
