/* Owned context publication, access checks and application TF control. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
static HANDLE ready,go;
static volatile LONG bad,hits_a,hits_b,steps;
__attribute__((naked,noinline)) static void watch_a(void){__asm__ volatile("nop\nret\n");}
__attribute__((naked,noinline)) static void watch_b(void){__asm__ volatile("nop\nret\n");}
__attribute__((naked,noinline)) static void alternate(void){__asm__ volatile("ret\n");}
extern char owned_app_step_return;
__attribute__((naked,noinline)) static void app_step(void){__asm__ volatile("pushfq\norq $0x100,(%rsp)\npopfq\nnop\n.globl owned_app_step_return\nowned_app_step_return:\nret\n");}
static void check(const char *label,BOOL pass){printf("CACHE check=%s pass=%u\n",label,pass);if(!pass)InterlockedIncrement(&bad);}
static LONG CALLBACK handler(EXCEPTION_POINTERS *p){
 if(p->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP)return EXCEPTION_CONTINUE_SEARCH;
 if(p->ContextRecord->Rip==(uintptr_t)watch_a || p->ContextRecord->Rip==(uintptr_t)watch_b){
  CONTEXT c={0};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
  if(!GetThreadContext(GetCurrentThread(),&c) || c.Dr0!=p->ContextRecord->Dr0 || !(c.Dr7&1) || !(p->ContextRecord->Dr6&1))InterlockedIncrement(&bad);
  if(p->ContextRecord->Rip==(uintptr_t)watch_a)InterlockedIncrement(&hits_a);else InterlockedIncrement(&hits_b);
  p->ContextRecord->Rip=(uintptr_t)alternate;return EXCEPTION_CONTINUE_EXECUTION;
 }
 if(p->ContextRecord->Rip==(uintptr_t)&owned_app_step_return){InterlockedIncrement(&steps);p->ContextRecord->EFlags&=~0x100;return EXCEPTION_CONTINUE_EXECUTION;}
 return EXCEPTION_CONTINUE_SEARCH;
}
static DWORD WINAPI worker(void *unused){
 (void)unused;CONTEXT c={0};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;c.Dr0=(uintptr_t)watch_a;c.Dr7=1;
 if(!SetThreadContext(GetCurrentThread(),&c))return 2;
 SetEvent(ready);WaitForSingleObject(go,INFINITE);
 for(unsigned i=0;i<16;++i){watch_a();watch_b();app_step();}
 return 0;
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);ready=CreateEventA(NULL,TRUE,FALSE,NULL);go=CreateEventA(NULL,TRUE,FALSE,NULL);PVOID veh=AddVectoredExceptionHandler(1,handler);
 HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL),minimal=NULL,denied=NULL;if(!ready||!go||!veh||!thread)return 2;
 if(WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0 || SuspendThread(thread)==(DWORD)-1)return 2;
 if(!DuplicateHandle(GetCurrentProcess(),thread,GetCurrentProcess(),&minimal,THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,0) ||
    !DuplicateHandle(GetCurrentProcess(),thread,GetCurrentProcess(),&denied,THREAD_QUERY_INFORMATION,FALSE,0))return 2;
 CONTEXT c={0};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;BOOL read=GetThreadContext(thread,&c);check("peer-debug",read && c.Dr0==(uintptr_t)watch_a && (c.Dr7&0xff)==1);
 c.ContextFlags=CONTEXT_DEBUG_REGISTERS;read=GetThreadContext(minimal,&c);check("minimal-rights",read && c.Dr0==(uintptr_t)watch_a && (c.Dr7&0xff)==1);
 c.ContextFlags=CONTEXT_DEBUG_REGISTERS;SetLastError(0);read=GetThreadContext(denied,&c);check("access-denied",!read && GetLastError()==ERROR_ACCESS_DENIED);
 c.ContextFlags=CONTEXT_FULL|CONTEXT_DEBUG_REGISTERS;read=GetThreadContext(minimal,&c);check("mixed-context",read && c.Rip && c.Rsp && c.Dr0==(uintptr_t)watch_a);
 ZeroMemory(&c,sizeof(c));c.ContextFlags=CONTEXT_DEBUG_REGISTERS;check("clear",SetThreadContext(thread,&c));
 c.ContextFlags=CONTEXT_DEBUG_REGISTERS;read=GetThreadContext(minimal,&c);check("clear-readback",read && !(c.Dr7&0xff));
 c.Dr0=(uintptr_t)watch_b;c.Dr7=1;check("replace",SetThreadContext(thread,&c));
 c.ContextFlags=CONTEXT_DEBUG_REGISTERS;read=GetThreadContext(minimal,&c);check("replace-readback",read && c.Dr0==(uintptr_t)watch_b && (c.Dr7&0xff)==1);
 if(ResumeThread(thread)==(DWORD)-1)return 2;
 SetEvent(go);DWORD wait=WaitForSingleObject(thread,10000),exit=0;GetExitCodeThread(thread,&exit);
 printf("CACHE summary bad=%ld hits_a=%ld hits_b=%ld application_steps=%ld wait=%lu worker_exit=%lu\n",bad,hits_a,hits_b,steps,(unsigned long)wait,(unsigned long)exit);
 CloseHandle(thread);CloseHandle(minimal);CloseHandle(denied);CloseHandle(ready);CloseHandle(go);RemoveVectoredExceptionHandler(veh);
 return bad || hits_a || hits_b!=16 || steps!=16 || wait!=WAIT_OBJECT_0 || exit;
}
