#include "tiktok/simon.hpp"

namespace tt
{

namespace
{

constexpr uint64_t kConstant = 0x3DC94C3A046D678BuLL;
constexpr int      kRounds   = 72;

inline uint64_t rotl(uint64_t v, unsigned n)
{
    n &= 63;
    return n == 0 ? v : (v << n) | (v >> (64 - n));
}

inline uint64_t rotr(uint64_t v, unsigned n)
{
    n &= 63;
    return n == 0 ? v : (v >> n) | (v << (64 - n));
}

inline uint64_t bit(uint64_t value, int pos) { return (value >> pos) & 1u; }

void expandKey(uint64_t key[kRounds], const SimonKey& k)
{
    key[0] = k[0];
    key[1] = k[1];
    key[2] = k[2];
    key[3] = k[3];

    for (int i = 4; i < kRounds; ++i)
    {
        uint64_t tmp = rotr(key[i - 1], 3);
        tmp ^= key[i - 3];
        tmp ^= rotr(tmp, 1);
        key[i] = ~key[i - 4] ^ tmp ^ bit(kConstant, (i - 4) % 62) ^ 3u;
    }
}

} // namespace

SimonBlock simonEncrypt(const SimonBlock& plain, const SimonKey& k, int mode)
{
    uint64_t key[kRounds];
    expandKey(key, k);

    uint64_t x0 = plain[0];
    uint64_t x1 = plain[1];

    for (int i = 0; i < kRounds; ++i)
    {
        const uint64_t tmp = x1;
        const uint64_t f   = mode == 1 ? rotl(x1, 1) : (rotl(x1, 1) & rotl(x1, 8));
        x1                 = x0 ^ f ^ rotl(x1, 2) ^ key[i];
        x0                 = tmp;
    }

    return { x0, x1 };
}

SimonBlock simonDecrypt(const SimonBlock& cipher, const SimonKey& k, int mode)
{
    uint64_t key[kRounds];
    expandKey(key, k);

    uint64_t x0 = cipher[0];
    uint64_t x1 = cipher[1];

    for (int i = kRounds - 1; i >= 0; --i)
    {
        const uint64_t tmp = x0;
        const uint64_t f   = mode == 1 ? rotl(x0, 1) : (rotl(x0, 1) & rotl(x0, 8));
        x0                 = x1 ^ f ^ rotl(x0, 2) ^ key[i];
        x1                 = tmp;
    }

    return { x0, x1 };
}

} // namespace tt
