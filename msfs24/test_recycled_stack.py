"""Owned stack-growth contracts in a NEW prefix. Never pass a game prefix."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime',required=True,type=Path)
    ap.add_argument('--cc',required=True,type=Path)
    ap.add_argument('--work',required=True,type=Path)
    ap.add_argument('--baseline',action='store_true',help='Expect the old recycled-stack failure')
    a=ap.parse_args();rt=a.runtime.resolve();work=a.work.resolve()
    if work.exists():ap.error('--work must be new')
    work.mkdir(parents=True,mode=0o700)
    env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','CX_','DYLD_','GST_','MSFS24_','MULLION_','DXMT_','ROSETTA_'))}
    env.update(WINEPREFIX=str(work/'prefix'),WINEDEBUG='-all',WINEMSYNC='0',ROSETTA_ADVERTISE_AVX='1',
        WINEDLLOVERRIDES='winedbg.exe=d;mscoree,mshtml=',MSFS24_WINE_HW_DEBUG='1',
        MSFS24_WINE_HW_CONTEXT_CACHE='1',MSFS24_WINE_GUARD_STEP='1',
        MSFS24_WINE_EAGER_STACK_GUARDS='1',MSFS24_WINE_STACK_PREFLIGHT='1',MSFS24_WINE_STACK_POLL='1',
        MSFS24_WINE_FIBER_STACK_GUARDS='0',MSFS24_WINE_SKIPPED_GUARD_COMMIT='1',
        MSFS24_WINE_RAISE_TRACE='1',MSFS24_WINE_GUARD_TARGET='owned_stack_contract.exe')
    def stop():
        for flag in ('-k','-w'):subprocess.run([str(rt/'bin/wineserver'),flag],env=env,capture_output=True,timeout=20)
    src=Path(__file__).resolve().parent/'recycled-stack-tests/owned_stack_contract.c';exe=work/'owned_stack_contract.exe'
    compile=[str(a.cc.expanduser().absolute()),'-O1','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)]
    with (work/'compile.log').open('x') as log:subprocess.run(compile,stdout=log,stderr=subprocess.STDOUT,check=True)
    rows=[];result=dict(module_sha256=sha(rt/'lib/wine/x86_64-unix/ntdll.so'),source_sha256=sha(src),
        exe_sha256=sha(exe),baseline=a.baseline,game_launched=False,cases=rows,compile_command=compile)
    try:
        with (work/'wineboot.log').open('x') as log:
            boot=subprocess.run([str(rt/'bin/wine'),'wineboot','-i'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        stop()
        if boot.returncode:raise RuntimeError('prefix initialization failed')
        for mode in ('clean','recycled','multiple','off','readonly','noaccess','committed','missing-guard','too-far','ordinary'):
            active=dict(env,MSFS24_WINE_SKIPPED_GUARD_COMMIT='0' if mode=='off' else '1')
            with (work/(mode+'.log')).open('x') as log:
                try:code=subprocess.run([str(rt/'bin/wine'),str(exe),mode],env=active,stdout=log,stderr=subprocess.STDOUT,timeout=20).returncode
                except subprocess.TimeoutExpired:code=124
                finally:stop()
            text=(work/(mode+'.log')).read_text(errors='replace')
            completed=mode in ('clean','ordinary') or (not a.baseline and mode in ('recycled','multiple'))
            oracle=f'CONTRACT mode={mode} completed={int(completed)} wait={0 if completed else 258} value={"1234" if completed else "0"} limit_offset={-53248 if completed else 0} pages_ok=1'
            passed=code==0 and oracle in text
            if completed:passed &= 'wine-platform-raise begin' not in text
            else:passed &= text.count('wine-platform-raise begin')==1 and 'wine-platform-skipped-guard commit' not in text
            rows.append(dict(mode=mode,passed=bool(passed),exit_code=code,oracle=oracle,log_sha256=sha(work/(mode+'.log'))))
            (work/'results.json').write_text(json.dumps(result,indent=2)+'\n')
            print(mode,'PASS' if passed else 'FAIL',flush=True)
            if not passed:print(text[-2400:]);break
    finally:
        stop();result['all_passed']=len(rows)==10 and all(r['passed'] for r in rows)
        (work/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['all_passed'] else 1
if __name__=='__main__':raise SystemExit(main())
