// Host-only hardware oracle for the pure integer reference used by FEX tests.
#include "division_cases.h"
#include <csignal>
#include <csetjmp>
#include <cstdio>
#include <ucontext.h>
#include <unistd.h>

namespace {
sigjmp_buf recovery;
volatile sig_atomic_t active = 0, trapCode = 0;
volatile uint32_t faultEAX = 0, faultEDX = 0, faultFlags = 0;
void divideFault(int, siginfo_t* info, void* raw) {
  if (!active) _exit(90);
  auto* context = static_cast<ucontext_t*>(raw);
  faultEAX = context->uc_mcontext.gregs[REG_RAX];
  faultEDX = context->uc_mcontext.gregs[REG_RDX];
  faultFlags = context->uc_mcontext.gregs[REG_EFL];
  trapCode = info->si_code;
  siglongjmp(recovery, 1);
}
bool check(armxbox::division::Input input, unsigned bits, bool isSigned) {
  const auto expected = armxbox::division::reference(input, bits, isSigned);
  uint32_t a = input.eax, d = input.edx;
  active = 1;
  if (sigsetjmp(recovery, 1)) {
    active = 0;
    return expected.fault && trapCode == FPE_INTDIV && faultEAX == input.eax &&
           faultEDX == input.edx && (faultFlags & 0x8d5) == 0x8d5;
  }
  // Seed arithmetic flags with DF clear, then divide without intervening code.
#define RUN_DIV(op) asm volatile("pushq $0xad7; popfq; " op " %%cl" : "+a"(a), "+d"(d) : "c"(input.ecx) : "cc", "memory")
#define RUN_DIV_W(op) asm volatile("pushq $0xad7; popfq; " op " %%cx" : "+a"(a), "+d"(d) : "c"(input.ecx) : "cc", "memory")
#define RUN_DIV_L(op) asm volatile("pushq $0xad7; popfq; " op " %%ecx" : "+a"(a), "+d"(d) : "c"(input.ecx) : "cc", "memory")
  if (bits == 8) { if (isSigned) { RUN_DIV("idivb"); } else { RUN_DIV("divb"); } }
  else if (bits == 16) { if (isSigned) { RUN_DIV_W("idivw"); } else { RUN_DIV_W("divw"); } }
  else { if (isSigned) { RUN_DIV_L("idivl"); } else { RUN_DIV_L("divl"); } }
#undef RUN_DIV
#undef RUN_DIV_W
#undef RUN_DIV_L
  active = 0;
  return !expected.fault && a == expected.eax && d == expected.edx;
}
}
int main() {
  struct sigaction action{}, previous{};
  action.sa_sigaction = divideFault;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGFPE, &action, &previous)) return 2;
  unsigned total=0, failures=0;
  for (unsigned bits : {8U,16U,32U}) for (bool sign : {false,true}) {
    armxbox::division::cases(bits,[&](armxbox::division::Input input) {
      ++total;
      if (!check(input,bits,sign) && ++failures <= 5) std::printf("FAIL native reference bits=%u signed=%u eax=%08x edx=%08x ecx=%08x\n",bits,sign,input.eax,input.edx,input.ecx);
    });
  }
  sigaction(SIGFPE,&previous,nullptr);
  std::printf("%s native x86 DIV/IDIV oracle: %u cases\n",failures?"FAIL":"PASS",total);
  return failures?1:0;
}
