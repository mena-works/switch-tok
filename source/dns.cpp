#include "dns.hpp"

#include <cerrno>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <borealis.hpp>

namespace
{

constexpr const char* kResolver      = "8.8.8.8";
constexpr uint16_t    kResolverPort  = 53;
constexpr int         kTimeoutMs     = 4000;
constexpr size_t      kMaxPacket     = 512;

// Encodes "www.example.com" as 3www7example3com0.
void writeQName(std::vector<uint8_t>& out, const std::string& host)
{
    size_t start = 0;
    while (start < host.size())
    {
        size_t dot = host.find('.', start);
        if (dot == std::string::npos)
            dot = host.size();

        const size_t len = dot - start;
        if (len == 0 || len > 63)
            return;

        out.push_back(static_cast<uint8_t>(len));
        out.insert(out.end(), host.begin() + start, host.begin() + dot);
        start = dot + 1;
    }
    out.push_back(0);
}

// Names in answers may be compression pointers rather than literal labels, so
// skipping one is not simply "walk to the next zero byte".
bool skipName(const uint8_t* buf, size_t size, size_t& at)
{
    while (at < size)
    {
        const uint8_t len = buf[at];

        if ((len & 0xC0) == 0xC0)
        {
            at += 2;  // pointer, and it always terminates the name
            return at <= size;
        }

        at += 1;
        if (len == 0)
            return true;

        at += len;
    }
    return false;
}

} // namespace

namespace dns
{

bool resolveA(const std::string& host, std::string& outIp)
{
    if (host.empty())
        return false;

    std::vector<uint8_t> query;
    query.reserve(64);

    const uint16_t id = 0x2401;
    query.push_back(id >> 8);
    query.push_back(id & 0xFF);
    query.push_back(0x01);  // recursion desired
    query.push_back(0x00);
    query.push_back(0x00);
    query.push_back(0x01);  // one question
    for (int i = 0; i < 6; ++i)
        query.push_back(0x00);  // no answer/authority/additional records

    writeQName(query, host);
    query.push_back(0x00);
    query.push_back(0x01);  // A
    query.push_back(0x00);
    query.push_back(0x01);  // IN

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return false;

    timeval tv {};
    tv.tv_sec  = kTimeoutMs / 1000;
    tv.tv_usec = (kTimeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in server {};
    server.sin_family = AF_INET;
    server.sin_port   = htons(kResolverPort);
    server.sin_addr.s_addr = inet_addr(kResolver);

    bool ok = false;

    if (sendto(sock, query.data(), query.size(), 0, reinterpret_cast<sockaddr*>(&server),
               sizeof(server)) == static_cast<ssize_t>(query.size()))
    {
        uint8_t       reply[kMaxPacket];
        const ssize_t got = recv(sock, reply, sizeof(reply), 0);

        if (got > 12)
        {
            const size_t size    = static_cast<size_t>(got);
            const int    answers = (reply[6] << 8) | reply[7];

            size_t at = 12;
            if (skipName(reply, size, at))
            {
                at += 4;  // question type and class

                for (int i = 0; i < answers && at + 10 <= size && !ok; ++i)
                {
                    if (!skipName(reply, size, at))
                        break;

                    const int type     = (reply[at] << 8) | reply[at + 1];
                    const int rdlength = (reply[at + 8] << 8) | reply[at + 9];
                    at += 10;

                    if (at + rdlength > size)
                        break;

                    // Skip CNAMEs and anything else; only an A record carries
                    // the address, and chained answers put it further down.
                    if (type == 1 && rdlength == 4)
                    {
                        char text[16];
                        snprintf(text, sizeof(text), "%u.%u.%u.%u", reply[at], reply[at + 1],
                                 reply[at + 2], reply[at + 3]);
                        outIp = text;
                        ok    = true;
                    }

                    at += rdlength;
                }
            }
        }
    }

    close(sock);

    if (ok)
        brls::Logger::info("dns: {} -> {} (via {})", host, outIp, kResolver);
    else
        brls::Logger::warning("dns: {} not resolved via {}", host, kResolver);

    return ok;
}

bool probeTcp(const std::string& ip, int port)
{
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        brls::Logger::warning("tcp probe: socket() failed");
        return false;
    }

    timeval tv {};
    tv.tv_sec = 8;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    const int rc = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    const int err = errno;
    close(sock);

    if (rc == 0)
        brls::Logger::info("tcp probe: blocking connect to {}:{} OK", ip, port);
    else
        brls::Logger::warning("tcp probe: blocking connect to {}:{} failed rc={} errno={}", ip,
                              port, rc, err);

    return rc == 0;
}

} // namespace dns
