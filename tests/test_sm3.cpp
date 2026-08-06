// Host-side check of the SM3 port against values the Python library produced.
// Compile with plain g++, not the Switch toolchain: this runs here, fast, so a
// mismatch is caught in seconds instead of after a build-and-deploy cycle.
//
//   g++ -std=c++17 -I../source test_sm3.cpp ../source/tiktok/sm3.cpp -o test_sm3

#include <cstdio>
#include <string>
#include <vector>

#include "tiktok/sm3.hpp"

namespace
{

std::string hex(const std::vector<uint8_t>& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string        out;
    for (uint8_t b : bytes)
    {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
    }
    return out;
}

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

std::vector<uint8_t> range64()
{
    std::vector<uint8_t> v(64);
    for (int i = 0; i < 64; ++i)
        v[i] = static_cast<uint8_t>(i);
    return v;
}

int failures = 0;

void check(const std::vector<uint8_t>& in, const std::string& expected)
{
    const std::string got = hex(tt::sm3(in));
    const bool        ok  = got == expected;
    printf("  [%s] len=%zu\n", ok ? "OK" : "FAIL", in.size());
    if (!ok)
    {
        printf("      expected %s\n      got      %s\n", expected.c_str(), got.c_str());
        ++failures;
    }
}

} // namespace

int main()
{
    printf("SM3 port vs Python reference:\n");
    check(bytes(""), "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b");
    check(bytes("abc"), "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0");
    check(bytes("hello world"), "44f0061e69fa6fdfc290c494654a05dc0c053da7e5c52b84ef93a9d67d3fff88");
    check(range64(), "93566f236d157aae078d1ddb5cebdbba1520b5142e22a8915564345ba2ae1d63");
    check(std::vector<uint8_t>(100, 'a'),
          "0c105d5a46a65fdf0a0938283db2517ea87f176de84786f443cb78802aaa03de");
    check(bytes("The quick brown fox"),
          "6f37aadec4d9e0582eb956bdad690d8f142dab51f0216ed5527294347fe273d2");

    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
