#include "tiktok/sm3.hpp"

#include <array>

namespace tt
{

namespace
{

constexpr uint32_t kIV[8] = {
    0x7380166fu, 0x4914b2b9u, 0x172442d7u, 0xda8a0600u,
    0xa96f30bcu, 0x163138aau, 0xe38dee4du, 0xb0fb0e4eu,
};

// Rotate left, with the k==0 case handled: a 32-bit shift by 32 is undefined in
// C, and the source's rotate is reached with a zero count (rotl(TJ[j], j) at
// j==0), so it must be spelled out rather than left to the shift.
inline uint32_t rotl(uint32_t a, unsigned k)
{
    k &= 31;
    return k == 0 ? a : (a << k) | (a >> (32 - k));
}

inline uint32_t ff(uint32_t x, uint32_t y, uint32_t z, int j)
{
    return j < 16 ? (x ^ y ^ z) : ((x & y) | (x & z) | (y & z));
}

inline uint32_t gg(uint32_t x, uint32_t y, uint32_t z, int j)
{
    return j < 16 ? (x ^ y ^ z) : ((x & y) | (~x & z));
}

inline uint32_t p0(uint32_t x) { return x ^ rotl(x, 9) ^ rotl(x, 17); }
inline uint32_t p1(uint32_t x) { return x ^ rotl(x, 15) ^ rotl(x, 23); }

inline uint32_t tj(int j) { return j < 16 ? 0x79cc4519u : 0x7a879d8au; }

void compress(uint32_t v[8], const uint8_t* block)
{
    uint32_t w[68];
    for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);

    for (int j = 16; j < 68; ++j)
        w[j] = p1(w[j - 16] ^ w[j - 9] ^ rotl(w[j - 3], 15)) ^ rotl(w[j - 13], 7) ^ w[j - 6];

    uint32_t w1[64];
    for (int j = 0; j < 64; ++j)
        w1[j] = w[j] ^ w[j + 4];

    uint32_t a = v[0], b = v[1], c = v[2], d = v[3];
    uint32_t e = v[4], f = v[5], g = v[6], h = v[7];

    for (int j = 0; j < 64; ++j)
    {
        const uint32_t ss1 = rotl((rotl(a, 12) + e + rotl(tj(j), j)), 7);
        const uint32_t ss2 = ss1 ^ rotl(a, 12);
        const uint32_t tt1 = ff(a, b, c, j) + d + ss2 + w1[j];
        const uint32_t tt2 = gg(e, f, g, j) + h + ss1 + w[j];

        d = c;
        c = rotl(b, 9);
        b = a;
        a = tt1;
        h = g;
        g = rotl(f, 19);
        f = e;
        e = p0(tt2);
    }

    v[0] ^= a; v[1] ^= b; v[2] ^= c; v[3] ^= d;
    v[4] ^= e; v[5] ^= f; v[6] ^= g; v[7] ^= h;
}

} // namespace

std::vector<uint8_t> sm3(const std::vector<uint8_t>& message)
{
    std::vector<uint8_t> msg = message;
    const uint64_t       bitLength = static_cast<uint64_t>(message.size()) * 8;

    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0x00);

    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFF));

    uint32_t v[8];
    for (int i = 0; i < 8; ++i)
        v[i] = kIV[i];

    for (size_t off = 0; off < msg.size(); off += 64)
        compress(v, msg.data() + off);

    std::vector<uint8_t> digest(32);
    for (int i = 0; i < 8; ++i)
    {
        digest[i * 4]     = static_cast<uint8_t>(v[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(v[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(v[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(v[i]);
    }
    return digest;
}

} // namespace tt
