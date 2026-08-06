#pragma once

#include <cstdint>
#include <vector>

namespace tt
{

// SM3 hash (GB/T 32905-2016). 32-byte digest.
//
// Ported from the tiktok_signer Python package and checked byte-for-byte
// against it on the host before ever reaching the Switch -- a signature that is
// one bit wrong is rejected identically to no signature at all, so "looks
// plausible" is worthless here.
std::vector<uint8_t> sm3(const std::vector<uint8_t>& message);

} // namespace tt
