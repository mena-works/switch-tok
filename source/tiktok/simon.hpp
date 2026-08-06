#pragma once

#include <array>
#include <cstdint>

namespace tt
{

// Simon 128/256 block cipher, as the tiktok_signer package uses it. Blocks and
// keys are pairs/quads of 64-bit words. Checked against the Python on the host.
using SimonBlock = std::array<uint64_t, 2>;
using SimonKey   = std::array<uint64_t, 4>;

SimonBlock simonEncrypt(const SimonBlock& plain, const SimonKey& key, int mode = 0);
SimonBlock simonDecrypt(const SimonBlock& cipher, const SimonKey& key, int mode = 0);

} // namespace tt
