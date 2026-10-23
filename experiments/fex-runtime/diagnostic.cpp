#include "diagnostic.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <atomic>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <FEXCore/Utils/WritePriorityMutex.h>
#include <mutex>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifdef __aarch64__
extern "C" void armxbox_probe_execute_abi(void*, void*, void (*)(void*, void*), uint64_t*);
#endif
namespace {
constexpr uintptr_t Code = 0x10000000, Data = Code + 0x4000, Stack = Code + 0x8000;
constexpr size_t RegionSize = 0x10000;
std::atomic_flag Started = ATOMIC_FLAG_INIT;
void diagnosticLog(const char* text) {
#ifdef __ANDROID__
  __android_log_write(ANDROID_LOG_INFO, "ARMXBOX-FEX", text);
#else
  std::fprintf(stderr, "%s\n", text);
#endif
}
struct Report {
  char* output; size_t capacity; size_t used = 0; int failed = 0;
  void line(const char* format, ...) {
    if (used >= capacity) return;
    va_list args; va_start(args, format);
    int count = vsnprintf(output + used, capacity - used, format, args);
    va_end(args);
    if (count > 0) used += static_cast<size_t>(count) < capacity - used ? count : capacity - used - 1;
  }
  void check(const char* name, bool result) {
    failed += !result; line("%s %s\n", result ? "PASS" : "FAIL", name);
    char message[256]; snprintf(message, sizeof message, "%s %s", result ? "PASS" : "FAIL", name);
    diagnosticLog(message);
  }
};
// No Linux syscall forwarding. An unexpected guest syscall terminates the probe.
class Calls final : public FEXCore::HLE::SyscallHandler {
public:
  void HandleSyscall(FEXCore::Core::CpuStateFrame*) override { diagnosticLog("FAIL unsupported guest syscall"); _exit(90); }
  FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState*, uint64_t address) override {
    return address >= Code && address < Code + 0x4000 ? FEXCore::HLE::ExecutableRangeInfo{Code, 0x4000, false} :
                                                                      FEXCore::HLE::ExecutableRangeInfo{0, 0, false};
  }
  std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(FEXCore::Core::InternalThreadState*, uint64_t) override {
    return std::nullopt;
  }
};
}
int armxbox_fex_selftest(char* output, size_t capacity) {
  if (!output || !capacity) return 1;
  output[0] = 0;
  Report report{output, capacity};
  report.line("ARMXBOX FEX Phase 0.2; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8\n");
#ifdef VIXL_SIMULATOR
  report.line("Execution: host VIXL simulator; NOT Android device proof\n");
#else
  report.line("Execution: Android ARM64 native process\n");
#endif
  if (Started.test_and_set()) { report.line("UNSUPPORTED one run per process; relaunch app\n"); return 2; }
  long page = sysconf(_SC_PAGESIZE);
  report.line("Host page size: %ld\n", page);
  // Upstream allocator and interrupt-page protections contain 4 KiB assumptions.
  if (page != 4096) { report.line("UNSUPPORTED host page size; 16 KiB integration remains open\n"); return 2; }
  // A hint with exact-address verification never overwrites an existing mapping.
  void* mapping = mmap(reinterpret_cast<void*>(Code), RegionSize, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED || reinterpret_cast<uintptr_t>(mapping) != Code) {
    if (mapping != MAP_FAILED) munmap(mapping, RegionSize);
    report.line("UNSUPPORTED low guest address unavailable\n"); return 2;
  }
  LogMan::Throw::InstallHandler([](const char* text) { diagnosticLog(text); });
  LogMan::Msg::InstallHandler([](LogMan::DebugLevels, const char* text) { diagnosticLog(text); });
  diagnosticLog("FEX probe: allocator setup");
  FEXCore::Allocator::SetupHooks(page);
  diagnosticLog("FEX probe: config setup");
  FEXCore::Config::Initialize();
  FEXCore::Config::Load();
  FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "0");
#ifdef VIXL_SIMULATOR
  FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLE_VIXL_INDIRECT_RUNTIME_CALLS, "0");
#endif
  FEXCore::HostFeatures features{}; // Conservative ARMv8 baseline; no optional host extensions.
  features.DCacheLineLog2 = 4;
  features.ProcessFeatures.HostType = FEXCore::HostFeatures::HostTypeEnum::Linux;
  features.ProcessPID = getpid();
  features.CPUMIDRs.push_back(0);
  Calls calls;
  FEXCore::SignalDelegator signals;
  {
    diagnosticLog("FEX probe: context setup");
    auto context = FEXCore::Context::Context::CreateNewContext(features);
    context->SetSignalDelegator(&signals);
    context->SetSyscallHandler(&calls);
    diagnosticLog("FEX probe: dispatcher setup");
    if (!context->InitCore()) { report.line("FAIL InitCore\n"); return 1; }
    diagnosticLog("FEX probe: thread setup");
    auto* thread = context->CreateThread(nullptr);
    FEXCore::Allocator::RegisterTLSData(thread);
    auto& state = thread->CurrentFrame->State;
    // Pinned FEX dispatcher contract: Break resets the stack and publishes fault
    // metadata, but architectural registers still need spilling before stopping.
    auto& pointers = thread->CurrentFrame->Pointers;
    pointers.GuestSignal_SIGILL = pointers.ThreadStopHandlerSpillSRA;
    pointers.GuestSignal_SIGTRAP = pointers.ThreadStopHandlerSpillSRA;
    pointers.GuestSignal_SIGSEGV = pointers.ThreadStopHandlerSpillSRA;
    std::array<bool, 6> abiOK{true, true, true, true, true, true};
    auto execute = [&] {
#ifdef __aarch64__
      uint64_t checks[6]{};
      armxbox_probe_execute_abi(context.get(), thread, [](void* ctx, void* cpu) {
        // FEX's dispatcher preserves GPR/SIMD callee-saved state, but leaves
        // host FPCR/FPSR ownership to its frontend. Scope guest FP state here.
        uint64_t hostFPCR, hostFPSR;
        asm volatile("mrs %0, fpcr\n mrs %1, fpsr" : "=r"(hostFPCR), "=r"(hostFPSR) :: "memory");
        static_cast<FEXCore::Context::Context*>(ctx)->ExecuteThread(static_cast<FEXCore::Core::InternalThreadState*>(cpu));
        asm volatile("msr fpcr, %0\n msr fpsr, %1" :: "r"(hostFPCR), "r"(hostFPSR) : "memory");
      }, checks);
      for (size_t i = 0; i < abiOK.size(); ++i) abiOK[i] = abiOK[i] && checks[i];
#else
      context->ExecuteThread(thread);
#endif
    };
    FEXCore::Core::CPUState::gdt_segment gdt[32]{};
    gdt[1].D = 1; // IA-32 default operand size, L=0.
    state.segment_arrays[0] = gdt;
    state.cs_idx = 8;
    constexpr size_t ShadowSize = FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
    void* shadow = FEXCore::Allocator::mmap(nullptr, ShadowSize + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (shadow == MAP_FAILED) { report.line("FAIL call/return shadow stack allocation\n"); return 1; }
    thread->CallRetStackBase = static_cast<char*>(shadow) + page;
    if (mprotect(thread->CallRetStackBase, ShadowSize, PROT_READ | PROT_WRITE)) {
      report.line("FAIL call/return shadow stack protection\n"); return 1;
    }
    state.callret_sp = reinterpret_cast<uintptr_t>(thread->CallRetStackBase) + ShadowSize / 4;
    state.mxcsr = 0x1f80;
    context->SetFlagsFromCompactedEFLAGS(thread, 2);
    auto run = [&](const unsigned char* bytes, size_t length) {
      // Guest x86 bytes are data to the ARM host. FEX publishes its own ARM code.
      context->ClearCodeCache(thread);
      memcpy(mapping, bytes, length);
      state.rip = Code;
      state.gregs[FEXCore::X86State::REG_RSP] = Stack + 0x1000;
      diagnosticLog("FEX probe: execute fixture");
      execute();
    };
    const unsigned char arithmetic[] = {0xb8,0xff,0xff,0xff,0xff,0xbb,0,0,0,0,0x83,0xc0,1,0x83,0xd3,0,0xf4};
    run(arithmetic, sizeof arithmetic);
    report.check("IA32 arithmetic/carry and HLT exit", state.gregs[0] == 0 && state.gregs[3] == 1 && state.rip == Code + sizeof arithmetic - 1);
    const unsigned char sse[] = {0xb8,21,0,0,0,0x66,0x0f,0x6e,0xc0,0x66,0x0f,0xfe,0xc0,0x66,0x0f,0x7e,0xc0,0xf4};
    run(sse, sizeof sse);
    report.check("SSE2 packed integer", state.gregs[0] == 42);
    const unsigned char mmx[] = {0xb8,21,0,0,0,0x0f,0x6e,0xc0,0x0f,0xfe,0xc0,0x0f,0x7e,0xc0,0x0f,0x77,0xf4};
    run(mmx, sizeof mmx);
    report.check("MMX and EMMS", state.gregs[0] == 42);
    *reinterpret_cast<uint32_t*>(Data) = 0;
    const unsigned char x87[] = {0xdb,0xe3,0xd9,0xe8,0xd9,0xe8,0xde,0xc1,0xdb,0x1d,0,0x40,0,0x10,0xf4};
    run(x87, sizeof x87);
    report.check("x87 extended arithmetic/store", *reinterpret_cast<uint32_t*>(Data) == 2);
    *reinterpret_cast<uint32_t*>(Data + 0x10) = 0x12345678;
    state.fs_cached = Data;
    const unsigned char tls[] = {0x64,0xa1,0x10,0,0,0,0xf4};
    run(tls, sizeof tls);
    report.check("FS cached base load", state.gregs[0] == 0x12345678);
    // Resume at a known second entry without rebuilding or replacing guest state.
    const unsigned char resume[] = {0xb8,40,0,0,0,0xf4,0x83,0xc0,2,0xf4};
    run(resume, sizeof resume);
    state.rip = Code + 6;
    execute();
    report.check("run/resume retains GPR state", state.gregs[0] == 42 && state.rip == Code + 9);
    const auto isExit = [&](uint8_t signal, uint8_t trap, uint64_t pc, uint16_t error = 0) {
      const auto& fault = thread->CurrentFrame->SynchronousFaultData;
      return fault.FaultToTopAndGeneratedException && fault.Signal == signal &&
             fault.TrapNo == trap && fault.err_code == error && state.rip == pc;
    };
    report.check("HLT exit carries guest PC and generated exception metadata", isExit(SIGSEGV, 13, Code + 9));
    const unsigned char invalid[] = {0x0f,0x0b,0xb8,42,0,0,0,0xf4};
    state.gregs[0] = 0x1234;
    run(invalid, sizeof invalid);
    report.check("UD2 controlled exit preserves faulting PC and GPR", isExit(SIGILL, 6, Code) && state.gregs[0] == 0x1234);
    state.rip = Code + 2;
    execute();
    report.check("resume after handled UD2 reaches next fixture entry", isExit(SIGSEGV, 13, Code + 7) && state.gregs[0] == 42);
    const unsigned char breakpoint[] = {0xcc,0xb8,99,0,0,0,0xf4};
    state.gregs[0] = 17;
    run(breakpoint, sizeof breakpoint);
    report.check("INT3 controlled exit reports next PC without executing it", isExit(SIGTRAP, 3, Code + 1) && state.gregs[0] == 17);
    const unsigned char interrupt[] = {0xcd,0x20,0xf4};
    run(interrupt, sizeof interrupt);
    report.check("unsupported INT reports GP with exact selector error", isExit(SIGSEGV, 13, Code, 0x102));
    // A typed exit-based stdcall prototype, using controlled fixtures only.
    // Guest call -> HLT stub -> native body -> reverse guest callback -> resume.
    const unsigned char caller[] = {0x6a,22,0x6a,20,0xe8,0xf7,0,0,0,0xf4};
    const unsigned char callback[] = {0x8b,0x44,0x24,4,0x83,0xc0,1,0x66,0x0f,0xef,0xc0,0xdb,0xe3,0xc3};
    memset(mapping, 0xf4, 0x200);
    memcpy(reinterpret_cast<void*>(Code + 0x140), callback, sizeof callback);
    state.gregs[3] = 0x11223344;
    state.gregs[6] = 0x55667788;
    state.gregs[7] = 0x12344321;
    state.FCW = 0x77f;
    run(caller, sizeof caller);
    uint32_t esp = state.gregs[4];
    bool bridge = state.rip == Code + 0x100 && esp == Stack + 0x1000 - 12;
    if (bridge) {
      auto* arguments = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(esp));
      bridge = arguments[0] == Code + 9 && arguments[1] == 20 && arguments[2] == 22;
      uint32_t first = arguments[1], second = arguments[2];
      uint32_t savedFlags = context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0);
      FEXCore::Core::CPUState saved{};
      memcpy(&saved, &state, sizeof saved);
      auto reverse = [&](uint32_t value) -> uint32_t {
        auto* callbackStack = reinterpret_cast<uint32_t*>(Stack + 0x800);
        callbackStack[0] = Code + 0x180;
        callbackStack[1] = value;
        state.gregs[4] = Stack + 0x800;
        state.rip = Code + 0x140;
        execute();
        bridge &= state.rip == Code + 0x180 && state.gregs[4] == Stack + 0x804;
        return static_cast<uint32_t>(state.gregs[0]);
      };
      // Explicit uint32_t arguments and return; no untyped host pointer call.
      auto nativeAdd = [&](uint32_t left, uint32_t right) -> uint32_t { return reverse(left) + right - 1; };
      uint32_t result = nativeAdd(first, second);
      memcpy(&state, &saved, sizeof state);
      state.gregs[0] = result;
      state.gregs[4] = esp + 12; // stdcall: pop return address and two arguments.
      state.rip = Code + 9;
      execute();
      bridge &= state.gregs[0] == 42 && state.gregs[4] == Stack + 0x1000 &&
                state.gregs[3] == 0x11223344 && state.gregs[6] == 0x55667788 && state.gregs[7] == 0x12344321;
      report.check("callback restores FPU/SIMD/control/flags state",
                   memcmp(&state.xmm, &saved.xmm, sizeof state.xmm) == 0 &&
                   memcmp(state.mm, saved.mm, sizeof state.mm) == 0 &&
                   state.FCW == saved.FCW && state.mxcsr == saved.mxcsr &&
                   context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0) == savedFlags);
    }
    report.check("exit-based typed stdcall/reverse callback/stack cleanup", bridge);
    // True guest-address aliases, exercised through generated load/store code.
    int aliasFD = static_cast<int>(syscall(SYS_memfd_create, "armxbox-fex-alias", 1U));
    bool aliases = aliasFD >= 0 && ftruncate(aliasFD, page) == 0;
    if (aliases) {
      // MAP_FIXED is restricted to two pages already owned by this probe.
      void* first = mmap(reinterpret_cast<void*>(Data), page, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, aliasFD, 0);
      void* second = mmap(reinterpret_cast<void*>(Data + 0x1000), page, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, aliasFD, 0);
      aliases = first == reinterpret_cast<void*>(Data) && second == reinterpret_cast<void*>(Data + 0x1000);
      if (aliases) {
        const unsigned char access[] = {0xc7,0x05,0,0x40,0,0x10,0x78,0x56,0x34,0x12,0xa1,0,0x50,0,0x10,0xf4};
        run(access, sizeof access);
        aliases = state.gregs[0] == 0x12345678 && *reinterpret_cast<uint32_t*>(Data + 0x1000) == 0x12345678;
      }
    }
    if (aliasFD >= 0) close(aliasFD);
    report.check("generated direct accesses share backing aliases", aliases);
    const unsigned char original[] = {0xb8,1,0,0,0,0xf4};
    run(original, sizeof original);
    *reinterpret_cast<unsigned char*>(Code + 1) = 42;
    {
      std::scoped_lock lock(context->GetCodeInvalidationMutex());
      context->InvalidateCodeBuffersCodeRange(Code, sizeof original);
      context->InvalidateThreadCachedCodeRange(thread, Code, sizeof original);
    }
    state.rip = Code;
    execute();
    report.check("explicit code invalidation observes modified instruction", state.gregs[0] == 42);
#ifdef __aarch64__
    report.check("Android x18 unchanged across all engine entries/exits", abiOK[0]);
    report.check("native SP restored and 16-byte aligned at return boundary", abiOK[1]);
    report.check("AAPCS64 x19-x29 preserved", abiOK[2]);
    report.check("AAPCS64 d8-d15 low halves preserved", abiOK[3]);
    report.check("native FPCR restored after guest execution", abiOK[4]);
    report.check("native FPSR restored after guest execution", abiOK[5]);
#else
    report.line("UNVERIFIED Android x18/SP/GPR/SIMD/FPCR/FPSR witnesses: run native ARM64 APK\n");
#endif
    FEXCore::Allocator::UninstallTLSData(thread);
    context->DestroyThread(thread);
    FEXCore::Allocator::munmap(shadow, ShadowSize + 2 * page);
  }
  FEXCore::Config::Shutdown();
  // Process-owned allocator remains alive; never unmap live global FEX objects.
  munmap(mapping, RegionSize);
  report.line("Result: %s; raw-memory/MMIO, full bridge, 16K, Xbox CPU policy and performance gates remain open\n", report.failed ? "FAIL" : "CPU FIXTURES PASS");
  return report.failed ? 1 : 0;
}
