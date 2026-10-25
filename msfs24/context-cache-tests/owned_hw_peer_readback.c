/* Owned suspended peer: verify the public set/get debug-register round trip. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
static HANDLE ready,release_event;
static volatile LONG hit;
__attribute__((naked,noinline)) static void watched(void){__asm__ volatile("nop\nret\n");}
__attribute__((naked,noinline)) static void alternate(void){__asm__ volatile("ret\n");}
static LONG CALLBACK handler(EXCEPTION_POINTERS *p){
 if(p->ExceptionRecord->ExceptionCode==EXCEPTION_SINGLE_STEP && p->ContextRecord->Rip==(uintptr_t)watched){InterlockedIncrement(&hit);p->ContextRecord->Rip=(uintptr_t)alternate;return EXCEPTION_CONTINUE_EXECUTION;}
 return EXCEPTION_CONTINUE_SEARCH;
}
static DWORD WINAPI worker(void *unused){(void)unused;SetEvent(ready);WaitForSingleObject(release_event,INFINITE);watched();return 0;}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);ready=CreateEventA(NULL,TRUE,FALSE,NULL);release_event=CreateEventA(NULL,TRUE,FALSE,NULL);if(!ready||!release_event)return 2;
 PVOID veh=AddVectoredExceptionHandler(1,handler);HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);if(!veh||!thread)return 2;
 if(WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0 || SuspendThread(thread)==(DWORD)-1)return 2;
 CONTEXT set={0},get={0};set.ContextFlags=CONTEXT_DEBUG_REGISTERS;set.Dr0=(uintptr_t)watched;set.Dr7=1;
 BOOL stored=SetThreadContext(thread,&set);get.ContextFlags=CONTEXT_DEBUG_REGISTERS;BOOL read=GetThreadContext(thread,&get);
 BOOL match=stored && read && get.Dr0==set.Dr0 && (get.Dr7&0xff)==1;
 printf("PEER stored=%u read=%u dr0_matches=%u enabled_lane_matches=%u get_error=%lu\n",stored,read,get.Dr0==set.Dr0,(get.Dr7&0xff)==1,(unsigned long)GetLastError());
 if(ResumeThread(thread)==(DWORD)-1)return 2;SetEvent(release_event);if(WaitForSingleObject(thread,5000)!=WAIT_OBJECT_0)return 2;
 printf("PEER summary roundtrip=%u hardware_hits=%ld\n",match,hit);
 CloseHandle(thread);CloseHandle(ready);CloseHandle(release_event);RemoveVectoredExceptionHandler(veh);return match && hit==1?0:1;
}
