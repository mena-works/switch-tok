// Host check of the Simon port against Python. Each case round-trips too:
// decrypt(encrypt(x)) must return x, which catches a key-schedule bug that a
// one-directional test would miss.
//
//   g++ -std=c++17 -I../source test_simon.cpp ../source/tiktok/simon.cpp -o test_simon

#include <cstdio>

#include "tiktok/simon.hpp"

namespace
{

int failures = 0;

void check(tt::SimonBlock pt, tt::SimonKey k, int mode, uint64_t e0, uint64_t e1)
{
    const tt::SimonBlock enc = tt::simonEncrypt(pt, k, mode);
    const tt::SimonBlock dec = tt::simonDecrypt(enc, k, mode);

    const bool encOk   = enc[0] == e0 && enc[1] == e1;
    const bool roundOk = dec[0] == pt[0] && dec[1] == pt[1];

    printf("  [%s] encrypt  [%s] round-trip\n", encOk ? "OK" : "FAIL", roundOk ? "OK" : "FAIL");
    if (!encOk)
    {
        printf("      expected %016llx %016llx\n      got      %016llx %016llx\n",
               (unsigned long long)e0, (unsigned long long)e1, (unsigned long long)enc[0],
               (unsigned long long)enc[1]);
    }
    if (!encOk || !roundOk)
        ++failures;
}

} // namespace

int main()
{
    printf("Simon port vs Python reference:\n");

    check({ 0x0123456789abcdefULL, 0xfedcba9876543210ULL },
          { 0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL,
            0x4444444444444444ULL },
          0, 0xc1b73b35be123a0aULL, 0xd1b2a1f3d0b9a4c3ULL);

    check({ 0, 0 }, { 1, 2, 3, 4 }, 0, 0xf83255ca76186f30ULL, 0x17a6ea2950231f8bULL);

    check({ 0xffffffffffffffffULL, 0x0ULL },
          { 0xdeadbeefcafebabeULL, 0x0011223344556677ULL, 0x8899aabbccddeeffULL,
            0xf0e1d2c3b4a59687ULL },
          1, 0xb6ee8285b959dd0eULL, 0xfbcf7ab45ba2c186ULL);

    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
