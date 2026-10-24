#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include <initializer_list>

namespace armxbox::division {
struct Input { uint32_t eax, edx, ecx; };
struct Expected { bool fault; uint32_t eax, edx; };
inline int64_t signExtend(uint64_t value, unsigned bits) {
  const uint64_t sign = uint64_t{1} << (bits - 1);
  return std::bit_cast<int64_t>((value ^ sign) - sign);
}
// Independent integer reference: no FEX IR or native divide instructions copied.
inline Expected reference(Input input, unsigned bits, bool isSigned) {
  const uint64_t mask = (uint64_t{1} << bits) - 1;
  const uint64_t low = input.eax & mask;
  const uint64_t high = bits == 8 ? (input.eax >> 8) & mask : input.edx & mask;
  const uint64_t numerator = (high << bits) | low;
  const uint64_t denominator = input.ecx & mask;
  uint64_t quotient = 0, remainder = 0;
  bool fault = !denominator;
  if (!fault && isSigned) {
    const int64_t n = signExtend(numerator, bits * 2), d = signExtend(denominator, bits);
    if (n == std::numeric_limits<int64_t>::min() && d == -1) {
      fault = true; // Avoid undefined signed C++ division in the reference.
    } else {
      const int64_t q = n / d, r = n % d;
      const int64_t limit = int64_t{1} << (bits - 1);
      fault = q < -limit || q >= limit;
      quotient = static_cast<uint64_t>(q); remainder = static_cast<uint64_t>(r);
    }
  } else if (!fault) {
    quotient = numerator / denominator; remainder = numerator % denominator;
    fault = quotient > mask;
  }
  if (fault) return {true, input.eax, input.edx};
  if (bits == 8) return {false, (input.eax & 0xffff0000U) | static_cast<uint32_t>((remainder & 255) << 8) | static_cast<uint32_t>(quotient & 255), input.edx};
  return {false, static_cast<uint32_t>((input.eax & ~mask) | (quotient & mask)),
                 static_cast<uint32_t>((input.edx & ~mask) | (remainder & mask))};
}
template<typename F> void cases(unsigned bits, F&& emit) {
  const uint32_t edges[] = {0,1,2,3,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
  // Cartesian boundary coverage includes wide dividend truncation, sign changes,
  // zero divisors, signed-min/-1 and preserved upper-register bytes.
  for (uint32_t a : edges) for (uint32_t d : edges) for (uint32_t c : edges) emit(Input{a,d,c});
  uint32_t random = 0x9e3779b9U ^ bits;
  auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
  for (unsigned i = 0; i < 1024; ++i) { uint32_t a=next(),d=next(),c=next(); emit(Input{a,d,c}); }
  // Construct valid quotient boundaries as well as one-past overflow, instead
  // of allowing random wide dividends to overwhelmingly select fault cases.
  const uint64_t mask = (uint64_t{1} << bits) - 1;
  for (uint32_t divisor : edges) {
    const uint64_t d = divisor & mask;
    for (uint64_t q : {uint64_t{0}, uint64_t{1}, mask >> 1, (mask >> 1) + 1, mask, mask + 1}) {
      const uint64_t n = q * d;
      uint32_t a = static_cast<uint32_t>(n & mask), high = static_cast<uint32_t>((n >> bits) & mask);
      if (bits == 8) a |= high << 8;
      emit(Input{a, high, divisor});
    }
  }
}
}
