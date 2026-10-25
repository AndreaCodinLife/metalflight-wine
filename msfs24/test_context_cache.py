#!/usr/bin/env python3
"""Owned-only Rosetta hardware-context regressions. Never use a game prefix.

Pass a candidate runtime and a NEW work directory. This creates its own prefix,
compiles authored code and exercises exact exception/count/address oracles.
Cache OFF deliberately reproduces two platform defects and counts those named
failures as expected negatives. No Steam, game or account data is needed.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime',type=Path,required=True)
    ap.add_argument('--cc',type=Path,required=True,help='Windows x86_64 cross compiler')
    ap.add_argument('--work',type=Path,required=True,help='New owned-only output directory')
    ap.add_argument('--cache',choices=['on','off'],default='on')
    ap.add_argument('--trace',action='store_true')
    args=ap.parse_args()
    # llvm-mingw dispatches its target from the invoked symlink basename.
    # Resolving the compiler symlink loses x86_64-w64-mingw32 and selects no target.
    runtime=args.runtime.resolve();work=args.work.resolve();cc=args.cc.expanduser().absolute()
    if work.exists():ap.error('--work must not already exist')
    if not (runtime/'bin/wine').is_file():ap.error('runtime has no bin/wine')
    if not cc.is_file():ap.error('cross compiler does not exist')
    work.mkdir(parents=True,mode=0o700)
    source=Path(__file__).resolve().parent/'context-cache-tests'
    prefix=work/'prefix'
    env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','CX_','DYLD_','GST_','MSFS24_','MULLION_','DXMT_','ROSETTA_'))}
    env.update(WINEPREFIX=str(prefix),WINEDEBUG='-all,warn+msfs_platform',
        WINEMSYNC='0',ROSETTA_ADVERTISE_AVX='1',WINEDLLOVERRIDES='winedbg.exe=d;mscoree,mshtml=',
        MSFS24_WINE_HW_DEBUG='1',MSFS24_WINE_SOFT_DEBUG='0',MSFS24_WINE_PAGE_DEBUG='0',
        MSFS24_WINE_GUARD_STEP='1',MSFS24_WINE_EAGER_STACK_GUARDS='1',
        MSFS24_WINE_STACK_PREFLIGHT='1',MSFS24_WINE_STACK_POLL='1',
        MSFS24_WINE_FIBER_STACK_GUARDS='0',MSFS24_WINE_SKIPPED_GUARD_COMMIT='1',
        MSFS24_WINE_PUSHF_FIX='1',MSFS24_WINE_POPF_FIX='1',MSFS24_WINE_RF_FIX='1',
        MSFS24_WINE_HW_CONTEXT_CACHE='1' if args.cache=='on' else '0',
        MSFS24_WINE_TRAP_RESULTS='1' if args.trace else '0')
    def stop():
        for flag in ('-k','-w'):
            subprocess.run([str(runtime/'bin/wineserver'),flag],env=env,capture_output=True,timeout=20)
    enabled=args.cache=='on'
    cases=[
        ('int3-after','owned_int3_hw_context',[],0,'OWNED summary int3=3072 hardware=2048 bad_address=0 bad_rip=0 unexpected=0'),
        ('int3-before','owned_int3_hw_context',['--hw-first'],0,'OWNED summary int3=3072 hardware=3072 bad_address=0 bad_rip=0 unexpected=0'),
        ('peer','owned_hw_peer_readback',[],0,'PEER summary roundtrip=1 hardware_hits=1'),
        ('cache-contract','owned_hw_cache_contract',[],0 if enabled else 1,
         f'CACHE summary bad={0 if enabled else 3} hits_a=0 hits_b=16 application_steps=16 wait=0 worker_exit=0'),
        ('popfq','cpu_flag_reference',['popfq_tf_ordering'],0,'SUMMARY fail=0 exit=0'),
        ('suspended-watch','owned_suspend_watch',[],0 if enabled else 1,
         f'SUSPEND summary query_correct={int(enabled)} hits={int(enabled)} breakpoints={int(not enabled)} wrong=0 wait=0 exit=0'),
    ]
    rows=[]
    result=dict(runtime=str(runtime),cache=args.cache,trace=args.trace,game_launched=False,
                ntdll_sha256=sha(runtime/'lib/wine/x86_64-unix/ntdll.so'),cases=rows)
    try:
        # Initialization is a separate operation: a timeout cannot masquerade as
        # an expected failing test, and its first log remains available.
        with (work/'wineboot.log').open('x') as log:
            boot=subprocess.run([str(runtime/'bin/wine'),'wineboot','-i'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        stop()
        if boot.returncode:raise RuntimeError('owned prefix initialization failed')
        for label,name,arguments,expected_code,oracle in cases:
            src=source/(name+'.c');exe=work/(name+'.exe')
            compile_command=[str(cc),'-O2','-Wall','-Wextra',str(src),'-o',str(exe)]
            with (work/(label+'-build.log')).open('x') as log:
                subprocess.run(compile_command,stdout=log,stderr=subprocess.STDOUT,check=True)
            command=[str(runtime/'bin/wine'),str(exe),*arguments]
            with (work/(label+'.log')).open('x') as log:
                try:
                    child=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45)
                    code=child.returncode
                except subprocess.TimeoutExpired:code=124
                finally:stop()
            text=(work/(label+'.log')).read_text(errors='replace')
            trace_count=text.count('trap-result')
            passed=code==expected_code and oracle in text and (trace_count<=64 if args.trace else trace_count==0)
            rows.append(dict(label=label,passed=passed,exit_code=code,expected_exit=expected_code,oracle=oracle,
                             source_sha256=sha(src),exe_sha256=sha(exe),log_sha256=sha(work/(label+'.log')),
                             command=command,compile_command=compile_command,trap_records=trace_count))
            (work/'results.json').write_text(json.dumps(result,indent=2)+'\n')
            print(label,'PASS' if passed else 'FAIL',flush=True)
            if not passed:break
    finally:
        stop()
        result['all_passed']=len(rows)==len(cases) and all(x['passed'] for x in rows)
        (work/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['all_passed'] else 1

if __name__=='__main__':raise SystemExit(main())
