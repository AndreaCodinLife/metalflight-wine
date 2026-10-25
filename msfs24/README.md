# MSFS24 Wine tree

## Recycled-stack correction candidate for MetalFlight issue 4

The opt-in `MSFS24_WINE_SKIPPED_GUARD_COMMIT=1` recovery now tolerates latent
guard bits on **uncommitted read/write** pages between the fault and the current
stack guard. `MEM_DECOMMIT` preserves those bits in Wine's page metadata. They
do not describe live guard pages, and rejecting them could leave exception
delivery writing into reserved stack memory. Recovery still starts at the
current committed guard and grows each page in order, leaving a new guard below
the fault. The existing view, protection, distance and stack-boundary checks
remain in effect; committed gaps and non-writable backing are refused.

`test_recycled_stack.py` compiles an authored stack fixture and creates a new,
owned-only prefix. It checks exact values, committed ranges, the new guard and
the lack of partial mutation on refused layouts. Run it on macOS under Rosetta:

```sh
python3 msfs24/test_recycled_stack.py --runtime /path/to/candidate-runtime \
  --cc /path/to/x86_64-w64-mingw32-clang --work /new/owned-test-directory
```

Use `--baseline` with the released dev7 runtime to require reproduction of the
two recycled-stack failures. Those are explicit expected failures, supervised
by another fixture thread so their unchanged page states can be checked. The
gate-off, read-only, no-access, committed-gap, missing-guard and distance cases
must remain refused. No game, Steam client or private prefix is used.

This candidate addresses an authored reproduction of the final stack-fault
shape in [MetalFlight issue 4](https://github.com/AndreaCodinLife/metalflight/issues/4).
The reporter's internal page metadata is absent from the logs. Actual A320
INIT A / FPLN recovery still requires game testing; this is not a confirmed
aircraft fix or a new runtime release.

The CodeWeavers CrossOver 26.3.0 Wine source (LGPL-2.1-or-later), plus the changes that the
MSFS24 macOS lab's selected runtime was built with. History:

| Commit | Content |
| --- | --- |
| CrossOver 26.3.0 Wine source, unmodified | `sources/wine` from `crossover-sources-26.3.0.tar.gz` (sha256 `ac99c8ca…`) |
| Platform patch (historical) | Stack guards, PUSHF/POPF/RF fixes, abort context, HTTP status trace, `server/mach.c` |
| Fiber guaranteed-stack argument | `__wine_prime_fiber_stack` receives each fiber's guaranteed stack bytes (default off) |
| ntdll Unix side | Platform result observers (default off); raise trace and skipped-guard commit (`signal_x86_64.c`, `virtual.c`) |
| Post-build ucrtbase formatter probe | `msfs24/ucrt_wcs_probe.py`: assembly-level stack probe in `pf_printf_a/_w` |
| Menu video fixes | mfmediaengine shared-write flush/wait and `SetLoop`; winegstreamer write-only output locks |

## Reproduction against the selected runtime

Each modified component of the lab's selected runtime (`menu22-fl121-media-20261022-v1`) was rebuilt
from this tree with the original build directory's rules and compilers: Homebrew mingw-w64 GCC 16.2
for PE, Apple clang for Unix, debug paths mapped. Equivalence means every non-debug section is
byte-equal; link timestamps and debug sections are excluded. The research workspace's
`tools/verify_msfs24_wine.py` reruns this from scratch.

| Component | Reference | Result |
| --- | --- | --- |
| ntdll.dll | 4a46fac2 | equivalent |
| kernelbase.dll | 030cfbaa | equivalent |
| winhttp.dll | 1c824026 | equivalent code, data and exports; one `ucrtbase` import hint differs (0x535 vs 0x534), which the loader only uses as a lookup hint |
| wineserver | 108d7d84 | equivalent |
| ntdll.so (staged) | f68a52da | equivalent |
| ucrtbase.dll (staged) | d79f3184 | equivalent, with `msfs24/ucrt_wcs_probe.py` |
| mfmediaengine.dll | 53a19e95 | equivalent |
| winegstreamer.dll | d9e09ef5 | sources equal the inputs of the recorded build (no build-platform rules) |

The source of two components was not preserved, and was reconstructed:
- `dlls/ntdll/loader.c` and `dlls/kernelbase/thread.c` at the fiber-transition "guarantee" stage
  (2026-10-14 11:55). They are the later wine-platform versions without the API-lookup and
  fiber-census observers that were added afterwards.
- `unixlib.h`, `ntdll.spec` and `winternl.h`: last edited 11:54:49, just before that build.

Not part of this tree:
- The Mullion renderer (`d3d10core`, `d3d11`, `d3d12`, `d3d12core`, `dxgi`, `winemetal`), built from
  its own repository.
- The lab helpers `abort_unix` and `mach_guard`, which only owned fixtures load.
- The CPU split-push helper, a separate DLL.

The rest of the selected runtime is CodeWeavers' own build. A full build of this tree produces new
binaries for it, and they need qualification.
