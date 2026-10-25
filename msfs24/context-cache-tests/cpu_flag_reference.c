/* Authored architectural flag/trap reference fixture. No game code, no helper,
 * no address or symbol matching of any kind.
 *
 * Purpose: run the SAME source on a native Windows x86-64 CPU and under the
 * Wine/Rosetta translation layer, then compare the two outputs per case. A
 * case that FAILS on real Windows means the fixture's expectation is wrong; a
 * case whose observed value DIFFERS between the two arms localises a
 * translation-layer divergence in flag/trap semantics.
 *
 * Every case prints exactly one line:
 *   CASE <name> <key>=<value> ... PASS|FAIL
 * Extra key=value fields are compared between arms too, so the differ does not
 * need to know what each field means.
 *
 * Scope is deliberately ring-3 instruction semantics only. SYSCALL entry via
 * LSTAR, and privileged MSRs, cannot be driven from user mode and are absent.
 *
 * Build (identical source on both arms):
 *   x86_64-w64-mingw32-gcc -O2 -Wall -Wextra cpu_flag_reference.c -o cpu_flag_reference.exe
 *   cl /O2 cpu_flag_reference.c                      (MSVC)
 *   x86_64-w64-mingw32-clang -O2 cpu_flag_reference.c -o cpu_flag_reference.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;

static volatile LONG   g_step_events;
static volatile LONG   g_watch_hits;
static volatile LONG   g_rf_on_break;      /* RF seen in the delivered trap image */
static volatile LONG64 g_step_rip_delta;
static volatile LONG64 g_step_flags;
static BYTE  *g_code;
static DWORD64 g_entry;

/* One vectored handler for every case; the case reads whichever counters it
 * needs. Diagnostic only: it never continues a fault it does not understand. */
static LONG CALLBACK veh(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *er = info->ExceptionRecord;
    CONTEXT *c = info->ContextRecord;
    if (er->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    /* A TF single-step is dispatched with Dr6 == 0 and TF already cleared in
       the delivered context (both verified with a scratch probe: keying on
       EFlags&0x100, or on the Dr6 BS bit, counts zero). So the discriminator is
       "SINGLE_STEP with no breakpoint bits": a hardware breakpoint sets Dr6
       bits 0-3, a pure single-step leaves Dr6 clear. */
    if (c->Dr6 & 0xF) {                          /* a hardware breakpoint fired */
        InterlockedIncrement(&g_watch_hits);
        if (c->EFlags & 0x10000)                 /* RF as delivered, before we touch it */
            InterlockedIncrement(&g_rf_on_break);
        c->EFlags |= 0x10000;                    /* RF: do not refire this instruction */
    } else {                                     /* pure single-step */
        InterlockedIncrement(&g_step_events);
        g_step_rip_delta = (LONG64)(c->Rip - g_entry);
        g_step_flags = (LONG64)c->EFlags;
    }
    if (c->EFlags & 0x100)                       /* leave no TF set behind us */
        c->EFlags &= ~(DWORD)0x100;
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void *place(size_t off, const BYTE *bytes, size_t n)
{
    memcpy(g_code + off, bytes, n);
    return g_code + off;
}

static int arm(DWORD64 dr0, int enable)
{
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &c)) return 0;
    c.Dr0 = dr0;
    c.Dr7 = enable ? 1u : 0u;
    c.Dr6 = 0;
    return SetThreadContext(GetCurrentThread(), &c) ? 1 : 0;
}

/* ---- byte stubs (verified encodings, no assembler needed) ---- */

/* pushfq; pop rax; ret */
static const BYTE S_PUSHFQ[] = { 0x9C, 0x58, 0xC3 };
/* mov eax,-1; add eax,2; pushfq; pop rax; ret */
static const BYTE S_ARITH[]  = { 0xB8,0xFF,0xFF,0xFF,0xFF, 0x83,0xC0,0x02, 0x9C,0x58,0xC3 };
/* pushfq; and qword[rsp],~TF; popfq; nop; mov eax,42; ret */
static const BYTE S_CLEAR[]  = { 0x9C, 0x48,0x81,0x24,0x24,0xFF,0xFE,0xFF,0xFF,
                                 0x9D, 0x90, 0xB8,0x2A,0,0,0, 0xC3 };
/* mov rax,0x102; push rax; popfq; nop; mov eax,1; ret   (TF=1; IF=0) */
static const BYTE S_ORDER[]  = { 0x48,0xB8, 0x02,0x01,0,0,0,0,0,0, 0x50, 0x9D,
                                 0x90, 0xB8,0x01,0,0,0, 0xC3 };
#define ORDER_TRAP_DELTA 13u   /* after POPF then NOP -> RIP at the MOV */
/* RCX = &out[2]. mov r9,rsp; mov rdx,2; pushfq; pop rax; mov[rcx],rax;
   push rdx; popfw; pushfq; pop rax; mov[rcx+8],rax; mov rsp,r9; ret
   (Windows x64 passes the first argument in RCX. RSP is restored explicitly
   because POPFW advances RSP by only two bytes, which would otherwise leave
   the final RET reading a misaligned frame.) */
static const BYTE S_WIDTH16[] = {
    0x49,0x89,0xE1,
    0x48,0xBA, 0x02,0x00,0,0,0,0,0,0,
    0x9C, 0x58, 0x48,0x89,0x01,
    0x52, 0x66,0x9D,
    0x9C, 0x58, 0x48,0x89,0x41,0x08,
    0x4C,0x89,0xCC,
    0xC3
};
/* RCX = &out. mov rax,0x46; push rax; popfq; pushfq; pop rax; mov[rcx],rax; ret
   Reads back the flags after a 64-bit POPFQ that clears IF and sets PF/ZF. */
static const BYTE S_POPFQ_APPLIES[] = {
    0x48,0xB8, 0x46,0x00,0,0,0,0,0,0,
    0x50, 0x9D,
    0x9C, 0x58, 0x48,0x89,0x01,
    0xC3
};
/* mov rax,0x46; push rax; popfq; jz +5; mov eax,0; jmp +5; mov eax,1; ret
   A conditional branch reads the NATIVE ZF, unlike PUSHFQ. Returns 1 when the
   POPFQ-set ZF took effect, 0 when it did not. This distinguishes "POPFQ not
   applied" from "PUSHFQ image stale". */
static const BYTE S_POPFQ_BRANCH[] = {
    0x48,0xB8, 0x46,0x00,0,0,0,0,0,0,
    0x50, 0x9D,
    0x74,0x07,                 /* jz +7  -> the mov eax,1 at 0x15 */
    0xB8,0x00,0x00,0x00,0x00,  /* mov eax,0 */
    0xEB,0x05,                 /* jmp +5 -> the ret */
    0xB8,0x01,0x00,0x00,0x00,  /* mov eax,1 */
    0xC3
};
/* IRET frame builder.  RCX = &params{target,cs,ss,rflags,out,cont}; iretq. */
static const BYTE S_IRET[] = {
    0x48,0x89,0xE0,             /* mov rax,rsp                */
    0x48,0x89,0x41,0x28,        /* mov [rcx+40],rax           */
    0x4C,0x8B,0x41,0x20,        /* mov r8,[rcx+32]            */
    0x48,0x83,0xEC,0x28,        /* sub rsp,40                 */
    0x48,0x8B,0x01,             /* mov rax,[rcx]              */
    0x48,0x89,0x04,0x24,        /* mov [rsp],rax              */
    0x48,0x8B,0x41,0x08,        /* mov rax,[rcx+8]            */
    0x48,0x89,0x44,0x24,0x08,   /* mov [rsp+8],rax            */
    0x48,0x8B,0x41,0x18,        /* mov rax,[rcx+24]           */
    0x48,0x89,0x44,0x24,0x10,   /* mov [rsp+16],rax           */
    0x48,0x8B,0x41,0x28,        /* mov rax,[rcx+40]           */
    0x48,0x89,0x44,0x24,0x18,   /* mov [rsp+24],rax           */
    0x48,0x8B,0x41,0x10,        /* mov rax,[rcx+16]           */
    0x48,0x89,0x44,0x24,0x20,   /* mov [rsp+32],rax           */
    0x48,0xCF                   /* iretq                      */
};
static const BYTE S_IRET_TARGET[] = { 0x9C, 0x58, 0x49,0x89,0x00, 0xC3 }; /* pushfq;pop rax;mov[r8],rax;ret */

typedef struct { u64 target, cs, ss, rflags, out, cont; } iret_params;

static int g_fail;
static const char *g_only;
static int want(const char *name) { return !strcmp(g_only, "all") || !strcmp(g_only, name); }

#define ARITH_MASK 0x8D5ull

int main(int argc, char **argv)
{
    g_only = (argc > 1 && argv[1][0] != '-') ? argv[1] : "all";
    setvbuf(stdout, NULL, _IONBF, 0);

    g_code = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
    if (!g_code) return 2;
    if (!AddVectoredExceptionHandler(1, veh)) return 2;

    void *st_pushfq = place(0x000, S_PUSHFQ, sizeof S_PUSHFQ);
    void *st_arith  = place(0x040, S_ARITH,  sizeof S_ARITH);
    void *st_clear  = place(0x080, S_CLEAR,  sizeof S_CLEAR);
    void *st_order  = place(0x0C0, S_ORDER,  sizeof S_ORDER);
    void *st_width  = place(0x100, S_WIDTH16,sizeof S_WIDTH16);
    void *st_popapp = place(0x180, S_POPFQ_APPLIES, sizeof S_POPFQ_APPLIES);
    void *st_popbr  = place(0x1C0, S_POPFQ_BRANCH,  sizeof S_POPFQ_BRANCH);
    void *st_iret   = place(0x200, S_IRET,   sizeof S_IRET);
    void *st_itgt   = place(0x2A0, S_IRET_TARGET, sizeof S_IRET_TARGET);

    printf("FLAG-REFERENCE build=1 arch=x86_64 page=%p\n", (void *)g_code);

    if (want("eflags_arith")) {
        u64 v = ((u64(*)(void))st_arith)();
        u64 m = v & ARITH_MASK;
        /* -1 + 2 = 1 : CF=1, AF=1, PF=0 (result 1 is odd), ZF=0, SF=0, OF=0 */
        int pass = (m == 0x11ull);
        printf("CASE %-22s observed=%#llx masked=%#llx expected=%#llx %s\n",
               "eflags_arith", (unsigned long long)v, (unsigned long long)m,
               (unsigned long long)0x11ull, pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("pushfq_unwatched")) {
        g_entry = (DWORD64)(uintptr_t)st_pushfq;
        g_step_events = 0;
        u64 v = ((u64(*)(void))st_pushfq)();
        int tf = (int)((v >> 8) & 1);
        int pass = (tf == 0);
        printf("CASE %-22s tf=%d expected=0 steps=%ld %s\n",
               "pushfq_unwatched", tf, (long)g_step_events, pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("pushfq_watched")) {
        int armed = arm((DWORD64)(uintptr_t)st_pushfq, 1);
        g_entry = (DWORD64)(uintptr_t)st_pushfq;
        g_step_events = 0; g_watch_hits = 0;
        u64 v = ((u64(*)(void))st_pushfq)();
        u64 v2 = ((u64(*)(void))st_pushfq)();
        int tf = (int)(((v | v2) >> 8) & 1);
        arm(0, 0);
        int pass = (tf == 0);
        printf("CASE %-22s armed=%d tf=%d expected=0 watch_hits=%ld steps=%ld %s\n",
               "pushfq_watched", armed, tf, (long)g_watch_hits, (long)g_step_events,
               pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("popfq_tf_ordering")) {
        g_entry = (DWORD64)(uintptr_t)st_order;
        g_step_events = 0; g_step_rip_delta = -1; g_step_flags = 0;
        u64 v = ((u64(*)(void))st_order)();
        LONG ev = g_step_events;
        LONG64 delta = g_step_rip_delta;
        int pass = (ev == 1 && delta == (LONG64)ORDER_TRAP_DELTA && v == 1);
        printf("CASE %-22s trap_events=%ld expected=1 trap_delta=%#llx expected_delta=%#x ret=%#llx %s\n",
               "popfq_tf_ordering", (long)ev, (unsigned long long)delta,
               ORDER_TRAP_DELTA, (unsigned long long)v, pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("popfq_width16")) {
        u64 out[2] = { 0, 0 };
        ((void(*)(u64 *))st_width)(out);
        u64 before = out[0], after = out[1];
        /* POPF cannot modify IF (0x200) or IOPL (0x3000) when CPL > IOPL, which
           is the ring-3 case here. Those bits are inherited from `before`; all
           other low-16 bits come from the pushed value, and bit1 is forced 1. */
        u64 locked = before & 0x3200ull;
        u64 expect_after = (before & ~0xFFFFull) | ((0x0002ull & ~0x3200ull) | locked);
        int pass = (after == expect_after);
        printf("CASE %-22s before=%#llx after=%#llx expected=%#llx %s\n",
               "popfq_width16", (unsigned long long)before, (unsigned long long)after,
               (unsigned long long)expect_after, pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("popfq_applies")) {
        u64 out = 0;
        ((void(*)(u64 *))st_popapp)(&out);
        /* Pushed 0x46 (IF=0, ZF=1, PF=1). IF cannot change at CPL3/IOPL0, so
           the incoming IF=1 is preserved: expected 0x246. ZF/PF must be set. */
        int pass = (out == 0x246ull);
        printf("CASE %-22s observed=%#llx expected=%#llx %s\n",
               "popfq_applies", (unsigned long long)out,
               (unsigned long long)0x246ull, pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("popfq_branch_effect")) {
        u64 v = ((u64(*)(void))st_popbr)();
        int pass = (v == 1ull);
        printf("CASE %-22s observed=%#llx expected=%#llx %s\n",
               "popfq_branch_effect", (unsigned long long)v,
               (unsigned long long)1ull, pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("clear_tf")) {
        g_entry = (DWORD64)(uintptr_t)st_clear;
        g_step_events = 0;
        u64 v = ((u64(*)(void))st_clear)();
        int pass = (v == 42 && g_step_events == 0);
        printf("CASE %-22s ret=%#llx expected=%#x steps=%ld %s\n",
               "clear_tf", (unsigned long long)v, 42u, (long)g_step_events,
               pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("rf_on_breakpoint")) {
        int armed = arm((DWORD64)(uintptr_t)st_pushfq, 1);
        g_rf_on_break = 0; g_watch_hits = 0;
        (void)((u64(*)(void))st_pushfq)();
        arm(0, 0);
        /* The unambiguous assertion is only that the breakpoint was delivered.
           Whether RF is present in the handler-visible trap image is a
           platform observation (Windows showed 0, Wine showed 1) and is left
           for the differ to compare, not asserted here. */
        int pass = (armed && g_watch_hits >= 1);
        printf("CASE %-22s armed=%d watch_hits=%ld rf_seen=%ld %s\n",
               "rf_on_breakpoint", armed, (long)g_watch_hits, (long)g_rf_on_break,
               pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    if (want("iret_flag_restore")) {
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        int ok = GetThreadContext(GetCurrentThread(), &c);
        u64 out = 0;
        iret_params p;
        p.target = (u64)(uintptr_t)st_itgt;
        p.cs     = c.SegCs;
        p.ss     = c.SegSs;
        p.rflags = 0x246ull;              /* IF|ZF|PF|bit1 : all legal, no reserved bits */
        p.out    = (u64)(uintptr_t)&out;
        p.cont   = 0;
        if (ok) ((void(*)(void *))st_iret)(&p);
        u64 expect = 0x246ull;
        int pass = (ok && out == expect);
        printf("CASE %-22s ok=%d observed=%#llx expected=%#llx cs=%#x ss=%#x %s\n",
               "iret_flag_restore", ok, (unsigned long long)out,
               (unsigned long long)expect, (unsigned)c.SegCs, (unsigned)c.SegSs,
               pass ? "PASS" : "FAIL");
        if (!pass) g_fail++;
    }

    printf("SUMMARY fail=%d exit=%d\n", g_fail, g_fail ? 1 : 0);
    return g_fail ? 1 : 0;
}
