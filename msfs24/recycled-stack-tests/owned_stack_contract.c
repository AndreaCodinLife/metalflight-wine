/* Authored recycled-fiber contracts. No game code or account data. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define RESERVE 0x180000
extern ULONG64 run_on_stack(void *, void *);
extern ULONG64 skip_write(void);
extern ULONG64 far_write(void);
extern ULONG64 descend(void);
__asm__(".text\n.globl run_on_stack\nrun_on_stack:\n"
        "pushq %rbp\n movq %rsp,%rbp\n movq %rcx,%rsp\n"
        "andq $-16,%rsp\n subq $32,%rsp\n callq *%rdx\n"
        "movq %rbp,%rsp\n popq %rbp\n retq\n"
        ".globl skip_write\nskip_write:\n subq $0xcb28,%rsp\n"
        "movl $0x1234,0x5c(%rsp)\n movl 0x5c(%rsp),%eax\n"
        "addq $0xcb28,%rsp\n retq\n"
        ".globl far_write\nfar_write:\n subq $0x50b28,%rsp\n"
        "movl $0x1234,0x5c(%rsp)\n movl 0x5c(%rsp),%eax\n"
        "addq $0x50b28,%rsp\n retq\n"
        ".globl descend\ndescend:\n movq %rsp,%rax\n movl $12,%ecx\n"
        "1:\n subq $0x1000,%rsp\n movq $0,(%rsp)\n decl %ecx\n jnz 1b\n"
        "movq %rax,%rsp\n movl $0x1234,%eax\n retq\n");
typedef struct {
    BYTE *reserve,*base;
    HANDLE ready,go;
    const char *mode;
    ULONG64 value;
    void *limit;
    LONG completed;
} fixture;
static int page_is(BYTE *p,DWORD state,DWORD protect)
{
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(p,&m,sizeof(m)) && m.State==state && m.Protect==protect;
}
static int latent_guard(BYTE *p,DWORD protect)
{
    return VirtualAlloc(p,0x1000,MEM_COMMIT,protect|PAGE_GUARD) &&
        VirtualFree(p,0x1000,MEM_DECOMMIT);
}
static DWORD WINAPI worker(void *arg)
{
    fixture *f=arg;
    BYTE *teb=(BYTE *)NtCurrentTeb(); NT_TIB *tib=(NT_TIB *)teb;
    void **dealloc=(void **)(teb+0x1478);
    void *oldbase=tib->StackBase,*oldlimit=tib->StackLimit,*olddealloc=*dealloc;
    SetEvent(f->ready); WaitForSingleObject(f->go,INFINITE);
    tib->StackBase=f->base; tib->StackLimit=f->base-0x9000; *dealloc=f->reserve;
    void *fn=!strcmp(f->mode,"too-far")?(void *)far_write:
        !strcmp(f->mode,"ordinary")?(void *)descend:(void *)skip_write;
    f->value=run_on_stack(f->base-0x100,fn); f->limit=tib->StackLimit;
    tib->StackBase=oldbase; tib->StackLimit=oldlimit; *dealloc=olddealloc;
    InterlockedExchange(&f->completed,1);
    return 0;
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if (argc!=2) return 2;
    fixture f={0}; f.mode=argv[1];
    f.reserve=VirtualAlloc(NULL,RESERVE,MEM_RESERVE,PAGE_READWRITE);
    if (!f.reserve) return 3;
    f.base=f.reserve+RESERVE;
    if (!VirtualAlloc(f.base-0x9000,0x9000,MEM_COMMIT,PAGE_READWRITE) ||
        !VirtualAlloc(f.base-0xa000,0x1000,MEM_COMMIT,PAGE_READWRITE|PAGE_GUARD)) return 3;
    int recycled=!strcmp(f.mode,"recycled") || !strcmp(f.mode,"multiple") || !strcmp(f.mode,"off");
    if (recycled && !latent_guard(f.base-0xb000,PAGE_READWRITE)) return 3;
    if (!strcmp(f.mode,"multiple") && !latent_guard(f.base-0xc000,PAGE_READWRITE)) return 3;
    if (!strcmp(f.mode,"readonly") && !latent_guard(f.base-0xb000,PAGE_READONLY)) return 3;
    if (!strcmp(f.mode,"noaccess")) {
        if (!VirtualAlloc(f.base-0xb000,0x1000,MEM_COMMIT,PAGE_NOACCESS) ||
            !VirtualFree(f.base-0xb000,0x1000,MEM_DECOMMIT)) return 3;
    }
    if (!strcmp(f.mode,"committed") && !VirtualAlloc(f.base-0xb000,0x1000,MEM_COMMIT,PAGE_READWRITE)) return 3;
    if (!strcmp(f.mode,"missing-guard")) {
        DWORD previous;
        if (!VirtualProtect(f.base-0xa000,0x1000,PAGE_READWRITE,&previous)) return 3;
    }
    int known=recycled || !strcmp(f.mode,"readonly") || !strcmp(f.mode,"noaccess") ||
        !strcmp(f.mode,"committed") || !strcmp(f.mode,"missing-guard") ||
        !strcmp(f.mode,"too-far") || !strcmp(f.mode,"ordinary") || !strcmp(f.mode,"clean");
    if (!known) return 2;
    f.ready=CreateEventA(NULL,TRUE,FALSE,NULL); f.go=CreateEventA(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,worker,&f,0,NULL);
    if (!thread || WaitForSingleObject(f.ready,5000)!=WAIT_OBJECT_0) return 3;
    SetEvent(f.go);
    DWORD wait=WaitForSingleObject(thread,1200);
    DWORD exit=999;
    if (wait==WAIT_OBJECT_0) GetExitCodeThread(thread,&exit);
    int pages_ok;
    if (f.completed) {
        pages_ok=1;
        for (int i=1;i<=13;i++) pages_ok &= page_is(f.base-i*0x1000,MEM_COMMIT,PAGE_READWRITE);
        pages_ok &= page_is(f.base-0xe000,MEM_COMMIT,PAGE_READWRITE|PAGE_GUARD);
        pages_ok &= page_is(f.base-0xf000,MEM_RESERVE,0);
        pages_ok &= page_is(f.reserve,MEM_RESERVE,0);
        pages_ok &= f.value==0x1234 && f.limit==f.base-0xd000 && exit==0;
    } else {
        pages_ok=page_is(f.base-0xd000,MEM_RESERVE,0) && page_is(f.base-0xc000,MEM_RESERVE,0) &&
            page_is(f.base-0xb000,!strcmp(f.mode,"committed")?MEM_COMMIT:MEM_RESERVE,
                !strcmp(f.mode,"committed")?PAGE_READWRITE:0) &&
            page_is(f.base-0xa000,MEM_COMMIT,!strcmp(f.mode,"missing-guard")?PAGE_READWRITE:PAGE_READWRITE|PAGE_GUARD) &&
            page_is(f.reserve,MEM_RESERVE,0);
    }
    printf("CONTRACT mode=%s completed=%ld wait=%lu value=%llx limit_offset=%lld pages_ok=%d\n",
        f.mode,f.completed,(unsigned long)wait,(unsigned long long)f.value,
        f.limit?(long long)((BYTE *)f.limit-f.base):0,pages_ok);
    /* A refused write may be stuck delivering its exception on the worker's
     * reserved stack. The supervisor verified no partial mutation above. */
    ExitProcess(pages_ok?0:1);
}
