#include "diagnostic.h"
#include "execution_boundary.h"
#include "cpu_identity_probe.h"
#include "division_cases.h"
#include "pc_metadata_probe.h"
#include "host_arena_probe.h"
#ifdef __ANDROID__
#include "raw_fault_probe.h"
#endif
#include <cstdlib>
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
  report.line("ARMXBOX FEX Phase 0.6; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8\n");
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
  auto memorySnapshot = [&](const char *stage) {
    const auto memory = armxbox::hostMemorySnapshot();
    if (memory.available) {
      report.line("Process memory %s: virtual=%zu KiB RSS=%zu KiB\n",
                  stage, memory.virtualKiB, memory.residentKiB);
    } else {
      report.line("Process memory %s: unavailable\n", stage);
    }
  };
  memorySnapshot("before arena");
  const auto arena = armxbox::prepareHostArena(Code, diagnosticLog);
  memorySnapshot("after arena/host controls");
  const char *arenaLabels[] = {
    "arena rejects incompatible host pages without changing hooks",
    "arena rejects undersized reservation without changing hooks",
    "arena rejects unaligned reservation without changing hooks",
    "arena rejects overflow reservation without changing hooks",
    "bounded high-VA arena initializes",
    "bounded arena refuses repeated initialization",
    "pre-existing host mapping remains intact",
    "host mappings survive concurrent allocator initialization",
    "ordinary host malloc coexists with bounded arena",
    "ordinary host mmap coexists with bounded arena",
    "host threads and concurrent bounded mappings coexist",
    "fresh host library maps and executes after initialization",
    "bounded mapping supports partial free and exact reuse",
    "host hooks reject fixed mapping and unmap outside owned arena",
    "host hooks reject low guest mapping and unmap",
    "bounded arena rejects exhaustion and invalid lengths",
    "bounded file mapping shares backing with ordinary host mapping"
  };
  static_assert(std::size(arenaLabels) == armxbox::ArenaCheckCount);
  for (unsigned i = 0; i < arena.passed.size(); ++i) {
    report.check(arenaLabels[i], arena.passed[i]);
  }
  report.line("Host arena: base=%llx reserved=%zu bytes (virtual capacity, not RSS)\n",
              (unsigned long long)arena.base, arena.size);
  if (arena.detail[0]) {
    report.line("Arena detail: %s\n", arena.detail);
  }
  if (!arena.ready) {
    munmap(mapping, RegionSize);
    return 1;
  }
  diagnosticLog("FEX probe: config setup");
  FEXCore::Config::Initialize();
  FEXCore::Config::Load();
  FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "0");
  const bool singleBlock = std::getenv("ARMXBOX_PROBE_SINGLE_BLOCK") != nullptr;
  FEXCore::Config::Set(FEXCore::Config::CONFIG_MULTIBLOCK, singleBlock ? "0" : "1");
  report.line("Translation: %s\n", singleBlock ? "single-block" : "multiblock");
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
      const auto mask = armxbox::executeGuest(context.get(), thread);
#ifdef __aarch64__
      for (size_t i = 0; i < abiOK.size(); ++i) abiOK[i] = abiOK[i] && ((mask >> i) & 1);
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
    for (unsigned bits : {8U,16U,32U}) for (bool signedDivide : {false,true}) {
      context->ClearCodeCache(thread);
      const unsigned char code[] = {0x66, static_cast<unsigned char>(bits == 8 ? 0xf6 : 0xf7), static_cast<unsigned char>(signedDivide ? 0xf9 : 0xf1), 0xf4};
      const unsigned skip = bits == 16 ? 0 : 1;
      memcpy(mapping, code + skip, sizeof code - skip);
      const unsigned length = 3 - skip;
      unsigned total = 0, failures = 0, faultCases = 0, validCases = 0;
      armxbox::division::cases(bits, [&](armxbox::division::Input input) {
        const auto expected = armxbox::division::reference(input, bits, signedDivide);
        state.gregs[0] = input.eax; state.gregs[2] = input.edx; state.gregs[1] = input.ecx;
        context->SetFlagsFromCompactedEFLAGS(thread, 0xed7);
        const uint32_t flags = context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0);
        state.rip = Code;
        execute();
        bool ok = expected.fault ? isExit(SIGFPE, 0, Code) : isExit(SIGSEGV, 13, Code + length);
        ok &= state.gregs[0] == expected.eax && state.gregs[2] == expected.edx;
        ok &= static_cast<uint32_t>(state.gregs[1]) == input.ecx;
        if (expected.fault) {
          ++faultCases;
          ok &= context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0) == flags;
          ok &= thread->CurrentFrame->SynchronousFaultData.si_code == FPE_INTDIV;
        } else ++validCases;
        ++total;
        if (!ok && ++failures <= 3) report.line("DIV mismatch bits=%u signed=%u eax=%08x edx=%08x ecx=%08x expected_fault=%u actual_pc=%llx actual_eax=%08x actual_edx=%08x\n", bits, signedDivide, input.eax,input.edx,input.ecx,expected.fault,(unsigned long long)state.rip,(uint32_t)state.gregs[0],(uint32_t)state.gregs[2]);
      });
      // Exercise deferred flags from a real guest ADD, then repair only the
      // divisor and resume at the faulting DIV/IDIV without replaying the ADD.
      context->ClearCodeCache(thread);
      const unsigned char prefix[] = {0xbb,0xff,0xff,0xff,0xff,0x83,0xc3,1};
      memcpy(mapping, prefix, sizeof prefix);
      memcpy(static_cast<char*>(mapping) + sizeof prefix, code + skip, length);
      const unsigned char afterDivide[]={0xb8,99,0,0,0,0xba,88,0,0,0,0xbb,77,0,0,0,0x83,0xc3,1,0xf4};
      memcpy(static_cast<char*>(mapping)+sizeof prefix+length,afterDivide,sizeof afterDivide);
      state.gregs[0]=17; state.gregs[2]=0; state.gregs[1]=0;
      context->SetFlagsFromCompactedEFLAGS(thread, 2);
      state.rip=Code;
      execute();
      bool deferred = isExit(SIGFPE, 0, Code + sizeof prefix) && state.gregs[0]==17 && state.gregs[2]==0 && state.gregs[3]==0;
      deferred &= (context->ReconstructCompactedEFLAGS(thread, false, nullptr, 0) & 0x8d5) == 0x55;
      failures += !deferred; ++total; ++faultCases;
      state.gregs[1]=5;
      // The faulting PC is retained; the adapter does not skip the instruction.
      execute();
      bool resumed = isExit(SIGSEGV,13,Code + sizeof prefix + length + sizeof afterDivide - 1);
      resumed &= state.gregs[0]==99 && state.gregs[2]==88 && state.gregs[3]==78;
      resumed &= (context->ReconstructCompactedEFLAGS(thread,false,nullptr,0) & 0x8d5)==4;
      failures += !resumed; ++total; ++validCases;
      char label[160]; snprintf(label,sizeof label,"%s%u reference matrix: %u cases (%u valid, %u faults)",signedDivide?"IDIV":"DIV",bits,total,validCases,faultCases);
      report.check(label, failures == 0 && faultCases && validCases);
      // Memory and register aliases must use the pre-instruction divisor.
      context->ClearCodeCache(thread);
      unsigned char memoryCode[] = {0x66, static_cast<unsigned char>(bits==8?0xf6:0xf7), static_cast<unsigned char>(signedDivide?0x3d:0x35),0,0x40,0,0x10,0xf4};
      memcpy(mapping,memoryCode+skip,sizeof memoryCode-skip);
      unsigned memoryTotal=0, memoryFailures=0;
      armxbox::division::cases(bits,[&](armxbox::division::Input input) {
        const auto expected=armxbox::division::reference(input,bits,signedDivide);
        *reinterpret_cast<uint32_t*>(Data)=input.ecx;
        state.gregs[0]=input.eax; state.gregs[2]=input.edx; state.gregs[1]=input.ecx;
        context->SetFlagsFromCompactedEFLAGS(thread,0xed7);
        const uint32_t flags=context->ReconstructCompactedEFLAGS(thread,false,nullptr,0);
        state.rip=Code; execute();
        bool ok=expected.fault?isExit(SIGFPE,0,Code):isExit(SIGSEGV,13,Code+7-skip);
        ok &= state.gregs[0]==expected.eax && state.gregs[2]==expected.edx && state.gregs[1]==input.ecx;
        if(expected.fault) ok &= context->ReconstructCompactedEFLAGS(thread,false,nullptr,0)==flags;
        ++memoryTotal; memoryFailures += !ok;
      });
      snprintf(label,sizeof label,"%s%u memory divisor matrix: %u cases",signedDivide?"IDIV":"DIV",bits,memoryTotal);
      report.check(label,memoryFailures==0);
      unsigned aliasTotal=0, aliasFailures=0;
      for(unsigned operand : {0U,2U,4U,6U}) {
        if(bits!=8 && operand>=4) continue;
        context->ClearCodeCache(thread);
        unsigned char aliasCode[]={0x66,static_cast<unsigned char>(bits==8?0xf6:0xf7),static_cast<unsigned char>((signedDivide?0xf8:0xf0)|operand),0xf4};
        memcpy(mapping,aliasCode+skip,sizeof aliasCode-skip);
        for(uint32_t seed=0;seed<256;++seed) {
          uint32_t a=seed*0x01010101U,d=(255-seed)*0x01010101U;
          uint32_t divisor=operand==0?a:operand==2?d:operand==4?a>>8:d>>8;
          auto expected=armxbox::division::reference({a,d,divisor},bits,signedDivide);
          state.gregs[0]=a; state.gregs[2]=d; state.gregs[1]=0xcafebabe;
          context->SetFlagsFromCompactedEFLAGS(thread,0xed7);
          const uint32_t flags=context->ReconstructCompactedEFLAGS(thread,false,nullptr,0);
          state.rip=Code; execute();
          bool ok=expected.fault?isExit(SIGFPE,0,Code):isExit(SIGSEGV,13,Code+length);
          ok &= state.gregs[0]==expected.eax && state.gregs[2]==expected.edx && state.gregs[1]==0xcafebabe;
          if(expected.fault) ok &= context->ReconstructCompactedEFLAGS(thread,false,nullptr,0)==flags;
          ++aliasTotal; aliasFailures += !ok;
        }
      }
      snprintf(label,sizeof label,"%s%u aliased dividend/divisor: %u cases",signedDivide?"IDIV":"DIV",bits,aliasTotal);
      report.check(label,aliasFailures==0);
    }
    using Run = decltype(execute);
    const auto metadata = armxbox::runPCMetadataProbe(
        context.get(), thread, Code, Data,
        [](void *run) { (*static_cast<Run *>(run))(); }, &execute);
    const char *metadataLabels[] = {
        "moffs load has exact fault-PC metadata",
        "moffs store has exact fault-PC metadata",
        "ModRM load has exact fault-PC metadata",
        "ModRM store has exact fault-PC metadata",
        "DIV memory has exact fault-PC metadata",
        "IDIV memory has exact fault-PC metadata"
    };
    for (unsigned i = 0; i < metadata.passed.size(); ++i) {
        report.check(metadataLabels[i], metadata.passed[i]);
    }
#ifdef VIXL_SIMULATOR
    for (unsigned i = 0; i < metadata.flags.size(); ++i) {
        char label[100];
        snprintf(label, sizeof label,
                 "simulated native memory access %u preserves pre-fault flags",
                 i);
        report.check(label, metadata.flags[i]);
    }
#endif
    if (metadata.detail[0]) {
        report.line("PC metadata detail: %s\n", metadata.detail);
    }
#ifdef __ANDROID__
    const auto raw=armxbox::runRawFaultProbe(context.get(),thread,Code,Code+RegionSize-4096,[](void* run){(*static_cast<Run*>(run))();},&execute);
    const char* rawLabels[]={"raw protected read: exact guest PC/registers/flags","typed MMIO read: one service and resume","raw protected write: exact guest PC/registers/flags","typed MMIO write: one service and resume","raw unmapped read: exact guest PC/registers/flags","typed unmapped MMIO read: service and resume","raw unmapped write: exact guest PC/registers/flags","typed unmapped MMIO write: service and resume","DIV memory fault precedes arithmetic error","IDIV memory fault precedes arithmetic error","original SIGSEGV registration restored"};
    for(unsigned i=0;i<raw.passed.size();++i) report.check(rawLabels[i],raw.passed[i]);
    if(raw.detail[0]) report.line("Raw-fault detail: %s\n",raw.detail);
#else
    report.line("UNVERIFIED native raw-fault/MMIO recovery: run Android ARM64 APK\n");
#endif
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
  diagnosticLog("FEX probe: Xbox CPU identity");
  const auto identity = armxbox::runCPUIdentityProbe(features, &calls, Code, Data, Stack);
  const char *identityLabels[] = {
    "CPU identity rejects unknown profile",
    "Xbox identity rejects 64-bit guest mode",
    "Xbox identity rejects shared disk cache",
    "Xbox identity rejects experimental code cache",
    "Xbox identity rejects code-cache validation",
    "Xbox CPUID API matches 54 reference cases twice",
    "Xbox CPUID name API is independent of host CPU number",
    "generated immediate CPUID matches 54 reference cases",
    "generated dynamic CPUID matches 54 reference cases",
    "CPUID preserves flags and unrelated GPRs at exact HLT exit",
    "default Xbox default contexts isolate identical guest addresses",
    "default FEX CPU identity retains modern engine capabilities"
  };
  static_assert(std::size(identityLabels) == std::size(identity.passed));
  for (unsigned i = 0; i < identity.passed.size(); ++i) {
    report.check(identityLabels[i], identity.passed[i]);
  }
#ifdef __aarch64__
  report.check("CPU identity guest entries preserve all six native ABI witnesses",
               identity.nativeEntries == 111 && identity.abiMask == 0x3f);
#endif
  if (identity.detail[0]) report.line("CPU identity detail: %s\n", identity.detail);
  const auto afterArena = armxbox::finishHostArena(arena.base, arena.size);
  report.check("host allocations and threads still coexist after all CPU fixtures", afterArena[0]);
  report.check("bounded arena still rejects exhaustion and services small allocations", afterArena[1]);
  memorySnapshot("after CPU fixtures");
  FEXCore::Config::Shutdown();
  // Process-owned allocator remains alive; never unmap live global FEX objects.
  munmap(mapping, RegionSize);
  report.line("Result: %s; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open\n", report.failed ? "FAIL" : "CPU FIXTURES PASS");
  return report.failed ? 1 : 0;
}
