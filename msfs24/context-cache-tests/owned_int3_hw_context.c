/* Owned code only: ordinary INT3 delivery before/after execution breakpoints. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
static unsigned traps,hardware,bad_address,bad_rip,unexpected,phase;
static BOOL wrong_expected;
__attribute__((naked,noinline)) static void owned_break(void){__asm__ volatile("int3\nret\n");}
__attribute__((naked,noinline)) static void owned_watch(void){__asm__ volatile("nop\nret\n");}
__attribute__((naked,noinline)) static void owned_alternate(void){__asm__ volatile("ret\n");}
static LONG CALLBACK handler(EXCEPTION_POINTERS *p){
 if(p->ExceptionRecord->ExceptionCode==EXCEPTION_SINGLE_STEP && p->ContextRecord->Rip==(uintptr_t)owned_watch){
  ++hardware;p->ContextRecord->Rip=(uintptr_t)owned_alternate;return EXCEPTION_CONTINUE_EXECUTION;
 }
 if(p->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT){
  ++traps;int address=p->ExceptionRecord->ExceptionAddress==(void*)((uintptr_t)owned_break+wrong_expected),rip=p->ContextRecord->Rip==(uintptr_t)owned_break;
  bad_address+=!address;bad_rip+=!rip;
  if((!address || !rip) && bad_address+bad_rip<=16)
   printf("OWNED mismatch phase=%u address_delta=%lld rip_delta=%lld\n",phase,(long long)((uintptr_t)p->ExceptionRecord->ExceptionAddress-(uintptr_t)owned_break),(long long)(p->ContextRecord->Rip-(uintptr_t)owned_break));
  p->ContextRecord->Rip=(uintptr_t)owned_break+1;return EXCEPTION_CONTINUE_EXECUTION;
 }
 ++unexpected;return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char **argv){
 BOOL off=FALSE,hw_first=FALSE;for(int i=1;i<argc;++i){off|=!lstrcmpA(argv[i],"--off");hw_first|=!lstrcmpA(argv[i],"--hw-first");wrong_expected|=!lstrcmpA(argv[i],"--wrong-address");}
 setvbuf(stdout,NULL,_IONBF,0);PVOID veh=AddVectoredExceptionHandler(1,handler);if(!veh)return 2;
 for(phase=0;phase<3;++phase){
  if(!off && phase==(hw_first?0u:1u)){CONTEXT c={0};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;c.Dr0=(uintptr_t)owned_watch;c.Dr7=1;if(!SetThreadContext(GetCurrentThread(),&c))return 2;}
  for(unsigned i=0;i<1024;++i){if(!off && (phase || hw_first))owned_watch();owned_break();}
  printf("OWNED phase=%u int3=%u hardware=%u bad_address=%u bad_rip=%u unexpected=%u\n",phase,traps,hardware,bad_address,bad_rip,unexpected);
 }
 CONTEXT c={0};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;if(!SetThreadContext(GetCurrentThread(),&c))return 2;
 RemoveVectoredExceptionHandler(veh);
 printf("OWNED summary int3=%u hardware=%u bad_address=%u bad_rip=%u unexpected=%u\n",traps,hardware,bad_address,bad_rip,unexpected);
 return traps!=3072 || hardware!=(off?0u:hw_first?3072u:2048u) || bad_address || bad_rip || unexpected;
}
