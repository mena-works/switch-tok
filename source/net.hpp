#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace net
{

void globalInit();
void globalExit();
void loadSessionId();

// Blocking GET into memory. Returns false on any transport or HTTP >= 400 error.
//
// maxBytes > 0 refuses the transfer up front when the server advertises a
// larger body, so an oversized file costs a request rather than a full download.
bool get(const std::string& url, std::vector<uint8_t>& out, long timeoutSeconds = 20,
         long maxBytes = 0);

bool getString(const std::string& url, std::string& out, long timeoutSeconds = 20);

// Percent-encodes a query value. Search terms arrive from the console keyboard
// and routinely contain spaces and non-ASCII.
std::string urlEncode(const std::string& value);

} // namespace net
