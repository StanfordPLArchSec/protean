#include "pin.H"
#include <iostream>
#include <elf.h>
#include <syscall.h>
#include <sys/prctl.h>
#include <asm/prctl.h>
#include <asm/unistd_64.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>
#include <set>

namespace gem5 {
  constexpr ADDRINT mmap_end = 0x6ffff7ff0000ULL;
  constexpr ADDRINT vsyscall_page = 0xffffffffff600000ULL;
}

static ADDRINT entrypoint = 0;
static std::string filename;
static std::vector<std::string> args;
static void *exe_base;
static bool use_host;
static unsigned long memsize = 512 * 1024 * 1024;
static int verbose = 0;
static FILE *syscall_file;
static ADDRINT vsyscall_base;
static ADDRINT vsyscall_size;
static std::map<ADDRINT, std::string> asmtab;


static void PIN_SafeCopyCheck(void *dst, const void *src, size_t size) {
  if (PIN_SafeCopy(dst, src, size) != size) {
    fprintf(stderr, "PIN_SafeCopy failed\n");
    abort();
  }
}

static std::string ReadString(ADDRINT app_addr) {
  std::string s;
  char c;
  do {
    PIN_SafeCopyCheck(&c, (const void *) app_addr, 1);
    ++app_addr;
    if (c)
      s.push_back(c);
  } while (c);
  return s;
}

void SafeMemset(void *dst_, int value, size_t size) {
  char *dst = (char *) dst_;
  for (size_t i = 0; i < size; ++i) {
    char c = value;
    PIN_SafeCopyCheck(dst + i, &c, 1);
  }
}


static void PushStack(uint64_t& sp, const void *data, size_t size) {
  sp -= size;
  PIN_SafeCopyCheck((void *) sp, data, size);
}

static void PushStackString(uint64_t& sp, const std::string& s) {
  PushStack(sp, s.c_str(), s.size() + 1);
}

static void PushStackZero(uint64_t& sp, size_t bytes) {
  char c = 0;
  for (size_t i = 0; i < bytes; ++i) {
    PushStack(sp, &c, 1);
  }
}

static void PushStackAlign(uint64_t& sp, size_t align) {
  PushStackZero(sp, sp & (align - 1));
}

static void PushStackAuxv(uint64_t& sp, uint64_t key, uint64_t value) {
  PushStack(sp, &value, sizeof value);
  PushStack(sp, &key, sizeof key);
  fprintf(stderr, "setting auxv type %lu value 0x%lx\n", key, value);
}

static ADDRINT ForceGetAuxv(ADDRINT type) {
  bool found = false;
  const ADDRINT value = PIN_GetAuxVectorValue(type, &found);
  if (!found) {
    fprintf(stderr, "failed to find aux vector value!\n");
    abort();
  }
  return value;
}

static void InitializeStack(ADDRINT *rsp_ptr) {
  static bool init = false;
  assert(!init);
  fprintf(stderr, "initializing stack...\n");
  const uint64_t old_rsp = *rsp_ptr;
  fprintf(stderr, "old rsp=%lx\n", old_rsp);

  uint64_t stack_end = old_rsp;
  {
    char c;
    while (PIN_SafeCopy(&c, (const void *) stack_end, 1))
      ++stack_end;
  }
  fprintf(stderr, "stack end=%lx\n", stack_end);

  // Get some information before we modify the stack
  [[maybe_unused]] const ADDRINT at_sysinfo_ehdr = ForceGetAuxv(AT_SYSINFO_EHDR);
  
  // Now, copy in data
  uint64_t sp = stack_end;

  // sentry
  char sentry[8];
  memset(sentry, 0, sizeof sentry);
  PushStack(sp, sentry, sizeof sentry);

  // filename
  PushStackString(sp, filename);
  fprintf(stderr, "filename %p\n", (void *) sp);

  // env data
  fprintf(stderr, "env %p\n", (void *) sp);

  // args data
  std::vector<ADDRINT> argptrs;
  for (auto it = args.rbegin(); it != args.rend(); ++it) {
    PushStackString(sp, *it);
    argptrs.push_back(sp);
  }
  fprintf(stderr, "args %p\n", (void *) sp);

  // align to 16B
  PushStackAlign(sp, 16);

  // aux data
  PushStackString(sp, "x86_64");
  const uint64_t aux_data_platform = sp;
  PushStackZero(sp, 16);
  const uint64_t aux_data_random = sp;
  fprintf(stderr, "auxdata %p\n", (void *) sp);

  // auxv array
  PushStackAlign(sp, 16);
  PushStackAuxv(sp, AT_NULL, 0);
  PushStackAuxv(sp, AT_PLATFORM, aux_data_platform);
  PushStackAuxv(sp, AT_EXECFN, 0); // we will fix this up later
  [[maybe_unused]] const uint64_t auxv_execfn_val = sp + 8;
  PushStackAuxv(sp, AT_RANDOM, aux_data_random);
  PushStackAuxv(sp, AT_SECURE, 0);
  PushStackAuxv(sp, AT_EGID, 100); // todo: might be funky
  PushStackAuxv(sp, AT_GID, 100);
  PushStackAuxv(sp, AT_EUID, 100);
  PushStackAuxv(sp, AT_UID, 100);
  PushStackAuxv(sp, AT_ENTRY, entrypoint);
  PushStackAuxv(sp, AT_FLAGS, 0);
  PushStackAuxv(sp, AT_BASE, 0); // NOTE: Only for static executables
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *) exe_base;
  PushStackAuxv(sp, AT_PHNUM, ehdr->e_phnum);
  assert(ehdr->e_phnum > 0);
  PushStackAuxv(sp, AT_PHENT, ehdr->e_phentsize);
  PushStackAuxv(sp, AT_PHDR, (ADDRINT)((char *) ehdr + ehdr->e_phoff));
  PushStackAuxv(sp, AT_CLKTCK, 100);
  PushStackAuxv(sp, AT_PAGESZ, 0x1000);
  PushStackAuxv(sp, AT_HWCAP, 0x78bfbff);
  PushStackAuxv(sp, AT_SYSINFO_EHDR, gem5::vsyscall_page);
  fprintf(stderr, "auxv %p\n", (void *) sp);

  // vsyscall stuff
  vsyscall_base = gem5::vsyscall_page;
  vsyscall_size = 0x1000;

  // envp array
  PushStackZero(sp, 8);

  // argv array
  PushStackZero(sp, 8);
  for (ADDRINT argptr : argptrs) {
    PushStack(sp, &argptr, sizeof argptr);
  }
  PIN_SafeCopyCheck((void *) auxv_execfn_val, &sp, sizeof sp);
  fprintf(stderr, "argv %p\n", (void *) sp);

  // argc
  const ADDRINT argc = args.size();
  PushStack(sp, &argc, sizeof argc);
  fprintf(stderr, "argc %p\n", (void *) sp);

  // clean up rest of stack
  if (old_rsp < sp) {
    for (ADDRINT x = old_rsp; x < sp; ++x) {
      char c = 0;
      PIN_SafeCopyCheck((void *) x, &c, 1);
    }
  }

  fprintf(stderr, "set rsp to %p (old %p)\n", (void *) sp, (void *) old_rsp);
  *rsp_ptr = sp;
  init = true;

  // dump register state?
}


static void InstrumentInstruction_InitializeStack(INS ins, void *) {
  assert(entrypoint != 0);
  if (INS_Address(ins) != entrypoint)
    return;

  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) InitializeStack, IARG_REG_REFERENCE, REG_RSP, IARG_END);
  
}


static FILE *trace_file = NULL;
static ADDRINT trace_last_inst = 0;
static void Trace_Default(void *inst) {
  fprintf(trace_file, "0x%lx\n", (ADDRINT) inst);
  trace_last_inst = (ADDRINT) inst;
}

static void Trace_StringOp(ADDRINT inst) {
  if (inst == trace_last_inst)
    return;
  fprintf(trace_file, "0x%lx\n", inst);
  trace_last_inst = inst;
}

void InstrumentInstruction_Trace(INS ins, void *) {
  if (INS_Opcode(ins) == XED_ICLASS_SYSCALL)
    return;
  if (INS_IsStringop(ins)) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) Trace_StringOp, IARG_INST_PTR, IARG_END);
  } else {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) Trace_Default, IARG_INST_PTR, IARG_END);
  }
  // fprintf(stderr, "trace: %p %s\n", (void *) INS_Address(ins), INS_Disassemble(ins).c_str());
}

static void HandleCPUID(ADDRINT *rax, ADDRINT *rbx, ADDRINT *rcx, ADDRINT *rdx) {
  const uint32_t in_eax = *rax;
  const uint32_t in_ecx = *rcx;

  if (in_eax == 0) {
    *rax = 0xd;
    *rbx = 0x6f677948;
    *rcx = 0x656e6975;
    *rdx = 0x6e65476e;
  } else if (in_eax == 1) {
    *rax=0x20f51;
    *rbx=0x805;
    *rcx=0x209;
    *rdx=0xefdbfbff;
  } else if (in_eax == 7 && in_ecx <= 1) {
    *rax=0x0;
    *rbx=0x1800000;
    *rcx=0x0;
    *rdx=0x0;
  } else if (in_eax == 0xd && in_ecx == 1) {
    *rax=0x0;
    *rbx=0x0;
    *rcx=0x0;
    *rdx=0x0;
  } else if (in_eax == 0x80000000) {
    *rax=0x80000008;
    *rbx=0x6f677948;
    *rcx=0x656e6975;
    *rdx=0x6e65476e;
  } else if (in_eax == 0x80000001) {
    *rax=0x20f51;
    *rbx=0x405;
    *rcx=0x20001;
    *rdx=0xebd3fbff;
  } else if (in_eax == 0x80000007) {
    *rax=0x80000018;
    *rbx=0x68747541;
    *rcx=0x444d4163;
    *rdx=0x69746e65;
  } else if (in_eax == 0x80000008) {
    *rax=0x3030;
    *rbx=0x0;
    *rcx=0x0;
    *rdx=0x0;
  } else if (in_eax == 0x80000005) {
    *rax=0xff08ff08;
    *rbx=0xff20ff20;
    *rcx=0x40020140;
    *rdx=0x40020140;
  } else if (in_eax == 0x80000006) {
    *rax=0x0;
    *rbx=0x42004200;
    *rcx=0x4008140;
    *rdx=0x0;
  } else {
    fprintf(stderr, "cpuid eax 0x%x ecx 0x%x\n", in_eax, in_ecx);
    abort();
  }
}

#if 1
static bool sysemu;
static ADDRINT sysemu_return;

class Args;

using syscall_handler_t = bool (Args args, ADDRINT&);

class Args {
public:
  Args(CONTEXT *ctx, SYSCALL_STANDARD std): ctx(ctx), std(std) {}
  ADDRINT get(unsigned i) const {
    return PIN_GetSyscallArgument(ctx, std, i);    
  }
  ADDRINT operator()(unsigned i) const {
    return get(i);
  }
  void set(unsigned i, ADDRINT value) {
    PIN_SetSyscallArgument(ctx, std, i, value);
  }
  void operator()(unsigned i, ADDRINT value) {
    set(i, value);
  }
private:
  CONTEXT *ctx;
  SYSCALL_STANDARD std;
};

static bool HandleArchPrctl(Args args, ADDRINT& ret) {
  switch (args(0)) {
  case 0x3001:
  case ARCH_SET_FS:
    return false;
  default:
    fprintf(stderr, "arch_prctl: unhandled argument 0: 0x%lx\n", args(0));
    exit(1);
  }
}

static bool UnameSyscall(Args args, ADDRINT& ret) {
  fprintf(stderr, "warn: uname: not zero-filling unused bytes to mimic gem5\n");
  fprintf(stderr, "warn: uname: not including domainname\n");

  struct utsname *buf = (struct utsname *) args(0);
  const auto copy_field = [buf] (char *out, const char *in) {
    PIN_SafeCopyCheck(out, in, strlen(in) + 1);
  };

  copy_field(buf->sysname, "Linux");
  copy_field(buf->nodename, "sim.gem5.org");
  copy_field(buf->release, "5.1.0");
  copy_field(buf->version, "#1 Mon Aug 18 11:32:15 EDT 2003");
  copy_field(buf->machine, "x86_64");

  ret = 0;
  return true;
}

static bool GetRandomSyscall(Args args, ADDRINT& ret) {
  char *out = (char *) args(0);
  for (ADDRINT i = 0; i < args(1); ++i) {
    char c = 0x42;
    PIN_SafeCopyCheck(out, &c, 1);
  }
  ret = args(1);
  return true;
}

static ADDRINT mmap_begin = gem5::mmap_end;
bool MmapSyscall(Args args, ADDRINT& ret) {
  fprintf(syscall_file, "info: mmap: %p %zu %d %d %d %zd\n",
	  (void *) args(0), args(1), (int) args(2), (int) args(3), (int) args(4), args(5));
  if (args(0) == 0) {
    const size_t length = args(1);
    mmap_begin -= length;
    args(0, mmap_begin);
    args(3, args(3) | MAP_FIXED);
    return false;
  }
  
  fprintf(stderr, "stub: mmap: using host version, which diverges from gem5\n");
  return false;
}

bool MremapSyscall(Args args, ADDRINT& ret) {
  if (use_host)
    return false;
  fprintf(syscall_file, "info: mremap: %p %zu %zu %d %p\n",
	  (void *) args(0), args(1), args(2), (int) args(3), (void *) args(4));
  fprintf(stderr, "fatal: no support for mremap yet\n");
  exit(1);

  return false;
}

static bool SysInfoSyscall(Args args, ADDRINT& ret) {
  struct sysinfo *buf = (struct sysinfo *) args(0);
  unsigned long totalram = ::memsize;
  PIN_SafeCopyCheck(&buf->totalram, &totalram, sizeof(totalram));
  unsigned int mem_unit = 1;
  PIN_SafeCopyCheck(&buf->mem_unit, &mem_unit, sizeof(mem_unit));
  ret = 0;
  return true;
}

static bool ReadLinkSyscall(Args args, ADDRINT& ret) {
  const std::string path = ReadString(args(0));
  if (path == "/proc/self/exe") {
    fprintf(stderr, "readlink: returning -ENOENT for /proc/self/exe to mimic gem5\n");
    ret = -ENOENT;
    return true;
  }
  return false;
}

static bool OpenAtSyscall(Args args, ADDRINT& ret) {
  const std::string path = ReadString(args(1));
  fprintf(stderr, "info: openat: %d %s %d %d\n",
	  (int) args(0), path.c_str(), (int) args(2), (int) args(3));
  fprintf(stderr, "warn: using host implementation of openat, not gem5 emulation for now\n");
  return false;
}

static bool ReadSyscall(Args args, ADDRINT& ret) {
  fprintf(stderr, "info: read: %d %p %zu\n",
	  (int) args(0), (void *) args(1), args(2));
  fprintf(stderr, "warn: using host implementation of read, not gem5 emulation for now\n");
  return false;
}

static bool GetCWDSyscall(Args args, ADDRINT& ret) {
  fprintf(stderr, "info: getcwd: %p %zu\n",
	  (void *) args(0), args(1));
  fprintf(stderr, "warn: using host implementation for getcwd, not gem5 emulation for now\n");
  return false;
}

static bool BrkSyscall(Args args, ADDRINT& ret) {
  fprintf(syscall_file, "info: brk: %p\n", (void *) args(0));
  fprintf(stderr, "warn: using host implementation for brk\n");
  return false;
}

bool HostSyscall(Args, ADDRINT&) {
  return false;
}


static ADDRINT syscall_nr;
static void HandleSyscallEntry(THREADID threadIndex, CONTEXT *ctx, SYSCALL_STANDARD std, VOID *v) {
  sysemu = false;
  const ADDRINT nr = PIN_GetSyscallNumber(ctx, SYSCALL_STANDARD_IA32E_LINUX);
  syscall_nr = nr;
  Args args(ctx, std);

  fprintf(syscall_file, "syscall %lu at inst %p\n", nr, (void *) PIN_GetContextReg(ctx, REG_RIP));

  const auto unhandled_syscall = [nr] () {
    fprintf(stderr, "unhandled syscall: %lu\n", nr);
    exit(1);
  };

  static const std::map<ADDRINT, syscall_handler_t *> handlers = {
    {SYS_arch_prctl, HandleArchPrctl},
    {SYS_brk, BrkSyscall},
    {SYS_set_tid_address, HostSyscall},
    {SYS_set_robust_list, HostSyscall},
    {334, HostSyscall}, // rseq
    {SYS_uname, UnameSyscall},
    {SYS_prlimit64, HostSyscall},
    {318, GetRandomSyscall}, // getrandom
    {10, HostSyscall},
    {SYS_mmap, MmapSyscall},
    {SYS_sysinfo, SysInfoSyscall},
    {SYS_readlink, ReadLinkSyscall},
    {SYS_newfstatat, HostSyscall},
    {SYS_openat, OpenAtSyscall},
    {SYS_read, ReadSyscall},
    {SYS_getcwd, GetCWDSyscall},
    {SYS_close, HostSyscall},
    {SYS_write, HostSyscall},
    {SYS_munmap, HostSyscall},
    {SYS_mremap, MremapSyscall},
    {SYS_gettimeofday, HostSyscall},
  };

  const auto it = handlers.find(nr);
  if (it == handlers.end()) {
    if (use_host) {
      return;
    } else {
      unhandled_syscall();
    }
  }

  sysemu = it->second(args, sysemu_return);
  if (sysemu) {
    PIN_SetSyscallNumber(ctx, std, SYS_getpid);
  }
}

static void HandleSyscallExit(THREADID threadIndex, CONTEXT *ctx, SYSCALL_STANDARD std, VOID *v) {
  if (syscall_nr == SYS_set_tid_address) {
    sysemu = true;
    sysemu_return = 100;
  }
  if (sysemu) {
    PIN_SetContextReg(ctx, REG_RAX, sysemu_return);
  }
  fprintf(syscall_file, "syscall %d returned %ld.\n", (int) syscall_nr, PIN_GetSyscallReturn(ctx, std));
}
#endif

#if 1
static void HandleSyscall(CONTEXT *ctx) {
  const ADDRINT nr = PIN_GetContextReg(ctx, REG_RAX);
  ADDRINT sysemu_return;
  switch (nr) {
  case SYS_arch_prctl:
    fprintf(stderr, "stub: arch_prctl: 0x%x 0x%lx\n",
	    (uint32_t) PIN_GetContextReg(ctx, REG_RDI),
	    PIN_GetContextReg(ctx, REG_RSI));
    sysemu_return = -EINVAL;
    break;

  case SYS_brk:
    return;
    
  default:
    fprintf(stderr, "unhandled syscall: %lu\n", nr);
    exit(1);
  }
  
  const ADDRINT rip = PIN_GetContextReg(ctx, REG_RIP);
  const ADDRINT syscall_inst_size = 2;
  PIN_SetContextReg(ctx, REG_RIP, rip + syscall_inst_size);
  PIN_SetContextReg(ctx, REG_RAX, sysemu_return);
  PIN_ExecuteAt(ctx);
}
#endif

void InstrumentInstruction_Syscall(INS ins, void *v) {
  if (INS_IsSyscall(ins)) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) HandleSyscall,
		   IARG_CONTEXT, IARG_END);
  }
}

static void InstrumentInstruction_CPUID(INS ins, void *) {
  if (INS_Opcode(ins) == XED_ICLASS_CPUID) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) HandleCPUID,
		   IARG_REG_REFERENCE, REG_RAX,
		   IARG_REG_REFERENCE, REG_RBX,
		   IARG_REG_REFERENCE, REG_RCX,
		   IARG_REG_REFERENCE, REG_RDX,
		   IARG_END);
    INS_Delete(ins);
  }
}

static void HandleVsyscallRead(void *inst, void *addr, CONTEXT *ctx, ADDRINT inst_size) {
  if ((ADDRINT) addr >= vsyscall_base && (ADDRINT) addr < vsyscall_base + vsyscall_size) {
    const std::string& disasm = asmtab.at((ADDRINT) inst);
    fprintf(stderr, "vsyscall read: pc %p addr %p asm \"%s\"\n", inst, addr, disasm.c_str());
    if (disasm == "mov rax, qword ptr [rdi+0x20]") {
      PIN_SetContextReg(ctx, REG_RAX, 0);
    } else if (disasm == "movzx r8d, word ptr [rdi+0x38]") {
      PIN_SetContextReg(ctx, REG_R8, 0);
    } else if (disasm == "mov rax, qword ptr [rdx]") {
      PIN_SetContextReg(ctx, REG_RAX, 0);
    } else {
      fprintf(stderr, "fatal: unhandled vsyscall access\n");
      exit(1);
    }
    PIN_SetContextReg(ctx, REG_RIP, PIN_GetContextReg(ctx, REG_RIP) + inst_size);
    PIN_ExecuteAt(ctx);
  }

}

static void HandleVsyscallWrite(void *inst, void *addr) {
  if ((ADDRINT) addr >= vsyscall_base && (ADDRINT) addr < vsyscall_base + vsyscall_size) {
    fprintf(stderr, "vsyscall write: pc %p addr %p\n", inst, addr);
    fprintf(stderr, "fatal: vsyscall write\n");
    exit(1);
  }
}

static void InstrumentInstruction_Vsyscall(INS ins, void *) {
  RTN rtn = INS_Rtn(ins);
  static const std::set<std::string> whitelist = {
    "_dl_non_dynamic_init",
  };
  if (whitelist.find(RTN_Name(rtn)) == whitelist.end())
    return;

  for (uint32_t i = 0; i < INS_MemoryOperandCount(ins); ++i) {
    if (INS_MemoryOperandIsRead(ins, i)) {
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) HandleVsyscallRead,
			       IARG_INST_PTR, IARG_MEMORYOP_EA, i, IARG_CONTEXT, IARG_ADDRINT, INS_Size(ins), IARG_END);
      asmtab[INS_Address(ins)] = INS_Disassemble(ins);
    }
    if (INS_MemoryOperandIsWritten(ins, i)) {
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) HandleVsyscallWrite,
			       IARG_INST_PTR, IARG_MEMORYOP_EA, i, IARG_END);
    }
  }
}

static void InstrumentImage(IMG img, void *v) {
  if (IMG_IsMainExecutable(img)) {
    entrypoint = IMG_EntryAddress(img);
    exe_base = (void *) IMG_LowAddress(img);
  }
}

static char **find_args_start(char **argv) {
  for (; *argv; ++argv) {
    if (strcmp(*argv, "--") == 0) {
      argv += 1;
      if (*argv == nullptr)
	return nullptr;
      return argv;
    }
  }
  return nullptr;
}

static KNOB<std::string> KnobTrace(KNOB_MODE_WRITEONCE, "pintool", "x", "", "specify trace file");
static KNOB<int> KnobHost(KNOB_MODE_WRITEONCE, "pintool", "H", "0", "use host system calls by default");
static KNOB<std::string> KnobSyscallLog(KNOB_MODE_WRITEONCE, "pintool", "s", "", "specify syscall log");
static KNOB<int> KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "v", "", "verbose mode");

int main(int argc, char *argv[]) {
  PIN_InitSymbols();
  if (PIN_Init(argc, argv))
    return 1;

  if (KnobTrace.Value().empty()) {
    trace_file = nullptr;
  } else {
    trace_file = fopen(KnobTrace.Value().c_str(), "w");
    if (trace_file == nullptr) {
      perror("fopen");
      return 1;
    }
  }

  syscall_file = stderr;
  if (!KnobSyscallLog.Value().empty())
    syscall_file = fopen(KnobSyscallLog.Value().c_str(), "w");

  char **args = find_args_start(argv);
  if (!args) {
    fprintf(stderr, "failed to find args\n");
    return 1;
  }

  ::filename = args[0];
  for (char **argp = args; *argp; ++argp) {
    ::args.push_back(*argp);
  }

  IMG_AddInstrumentFunction(InstrumentImage, 0);
  INS_AddInstrumentFunction(InstrumentInstruction_InitializeStack, 0);
  INS_AddInstrumentFunction(InstrumentInstruction_CPUID, 0);
  if (trace_file) {
    INS_AddInstrumentFunction(InstrumentInstruction_Trace, 0);
  }

#if 1
  PIN_AddSyscallEntryFunction(HandleSyscallEntry, nullptr);
  PIN_AddSyscallExitFunction(HandleSyscallExit, nullptr);
#endif
#if 0
  INS_AddInstrumentFunction(InstrumentInstruction_Syscall, 0);
#endif

  INS_AddInstrumentFunction(InstrumentInstruction_Vsyscall, 0);

  use_host = (KnobHost.Value() != 0);
  verbose = KnobVerbose.Value();

  if (const char *memsizestr = getenv("MEMSIZE"))
    memsize = strtoul(memsizestr, nullptr, 0);

  PIN_StartProgram();

  return 0;
}
