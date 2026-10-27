// Execute the actual cross-assembled witness under VIXL with positive/negative
// ARM callbacks. These controls validate the witness, not Android/FEX execution.
#include <aarch64/decoder-aarch64.h>
#include <aarch64/simulator-aarch64.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
  if (bytes.empty() || bytes.size() % 4) return 2;
  std::vector<uint32_t> code(bytes.size() / 4);
  std::memcpy(code.data(), bytes.data(), bytes.size());
  struct Control { const char* name; uint32_t instruction; int failedIndex; };
  const Control controls[] = {
    {"valid callback", 0xd503201f, -1}, // NOP
    {"clobbered x18", 0xd2800012, 0}, // MOV x18,#0 (only simulated)
    {"clobbered x19", 0xd2800013, 2},
    {"clobbered d8", 0x9e6703e8, 3}, // FMOV d8,xzr
    {"clobbered FPCR", 0xd51b441f, 4},
  };
  int failures = 0;
  for (const auto& control : controls) {
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Simulator simulator(&decoder);
    std::array<uint64_t, 6> checks{};
    std::array<uint32_t, 2> callback{control.instruction, 0xd65f03c0};
    simulator.WriteXRegister(18, 0x1234);
    simulator.WriteXRegister(2, reinterpret_cast<uintptr_t>(callback.data()));
    simulator.WriteXRegister(3, reinterpret_cast<uintptr_t>(checks.data()));
    simulator.RunFrom(reinterpret_cast<const vixl::aarch64::Instruction*>(code.data()));
    bool passed = true;
    // Status 5 is FPSR: the pinned VIXL treats its MRS/MSR as unimplemented.
    // Do not convert that missing model into a claimed FPSR validation.
    for (int i = 0; i < 5; ++i) passed &= checks[i] == static_cast<uint64_t>(i != control.failedIndex);
    std::printf("%s ABI witness control: %s\n", passed ? "PASS" : "FAIL", control.name);
    if (!passed) std::printf("  checks: %llu %llu %llu %llu %llu\n", (unsigned long long)checks[0], (unsigned long long)checks[1], (unsigned long long)checks[2], (unsigned long long)checks[3], (unsigned long long)checks[4]);
    failures += !passed;
  }
  std::puts("UNVERIFIED FPSR witness: pinned VIXL does not model FPSR system-register instructions");
  return failures ? 1 : 0;
}
