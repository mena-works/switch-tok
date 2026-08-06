#pragma once

#include <string>

namespace dns
{

// Resolves a hostname to an IPv4 address by talking to a public resolver over
// UDP, bypassing the system resolver entirely.
//
// This exists for emulators. Citron implements BSD sockets but not sfdnsres, so
// getaddrinfo fails and every request dies with "Couldn't resolve host name"
// even though the network itself works. A console needs none of this, which is
// why it is only ever used as a fallback.
//
// Returns false on any failure; callers fall back to the normal path.
bool resolveA(const std::string& host, std::string& outIp);

// Diagnostic: does a plain blocking TCP connect reach the address?
//
// curl uses non-blocking connects, and an emulator that reports EAGAIN there
// instead of EINPROGRESS makes curl give up on a connection that would have
// succeeded. This tells the two cases apart before anything is built on top.
bool probeTcp(const std::string& ip, int port);

} // namespace dns
