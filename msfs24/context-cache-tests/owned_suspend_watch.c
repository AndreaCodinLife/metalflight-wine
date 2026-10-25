/* Authored code: keep a hardware execution watch armed across peer suspension.
 * A breakpoint reported at this NOP is a misclassified execution watch, never
 * an instruction inserted by the fixture. No game files or account required. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

static volatile LONG ready, go, hits, breakpoints, wrong;
__attribute__((naked,noinline)) static void watched(void) { __asm__ volatile("nop\nret\n"); }
__attribute__((naked,noinline)) static void alternate(void) { __asm__ volatile("ret\n"); }

static LONG CALLBACK handler(EXCEPTION_POINTERS *p)
{
    DWORD code = p->ExceptionRecord->ExceptionCode;
    uintptr_t address = (uintptr_t)p->ExceptionRecord->ExceptionAddress;
    if (code == EXCEPTION_SINGLE_STEP && p->ContextRecord->Rip == (uintptr_t)watched)
    {
        ++hits;
        if (!(p->ContextRecord->Dr6 & 1) || p->ContextRecord->Dr0 != (uintptr_t)watched) ++wrong;
        p->ContextRecord->Rip = (uintptr_t)alternate;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_BREAKPOINT && address >= (uintptr_t)watched - 1 && address <= (uintptr_t)watched + 1)
    {
        ++breakpoints;
        printf("SUSPEND misclassified code=%08lx address_delta=%lld rip_delta=%lld entry_byte=%02x\n",
               (unsigned long)code, (long long)(address-(uintptr_t)watched),
               (long long)(p->ContextRecord->Rip-(uintptr_t)watched), *(unsigned char *)watched);
        p->ContextRecord->Rip = (uintptr_t)alternate;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    ++wrong;
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI worker(void *unused)
{
    CONTEXT c = {0};
    (void)unused;
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    c.Dr0 = (uintptr_t)watched;
    c.Dr7 = 1;
    if (!SetThreadContext(GetCurrentThread(), &c)) return 2;
    InterlockedExchange(&ready, 1);
    while (!go) __asm__ volatile("pause" ::: "memory");
    watched();
    c.Dr0 = c.Dr7 = 0;
    if (!SetThreadContext(GetCurrentThread(), &c)) return 2;
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    PVOID veh = AddVectoredExceptionHandler(1, handler);
    HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    CONTEXT c = {0};
    DWORD exit_code = 0;
    if (!veh || !thread) return 2;
    for (unsigned i=0; !ready && i<1000; ++i) Sleep(1);
    if (!ready) return 2;
    Sleep(20);
    if (SuspendThread(thread) == (DWORD)-1) return 2;
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    BOOL read = GetThreadContext(thread, &c);
    BOOL correct = read && c.Dr0 == (uintptr_t)watched && (c.Dr7 & 1);
    printf("SUSPEND query_read=%u query_correct=%u\n", read, correct);
    if (ResumeThread(thread) == (DWORD)-1) return 2;
    InterlockedExchange(&go, 1);
    DWORD wait = WaitForSingleObject(thread, 5000);
    GetExitCodeThread(thread, &exit_code);
    printf("SUSPEND summary query_correct=%u hits=%ld breakpoints=%ld wrong=%ld wait=%lu exit=%lu\n",
           correct, hits, breakpoints, wrong, (unsigned long)wait, (unsigned long)exit_code);
    CloseHandle(thread);
    RemoveVectoredExceptionHandler(veh);
    return !correct || hits != 1 || breakpoints || wrong || wait != WAIT_OBJECT_0 || exit_code;
}
