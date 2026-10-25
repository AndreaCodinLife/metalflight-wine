# MetalFlight Wine changes

This repository contains the Wine source used by MetalFlight, based on the
CrossOver 26.3 source release. Existing CPU, media and platform changes retain
their individual Git history. Build and dependency instructions are in the
[MetalFlight launcher repository](https://github.com/AndreaCodinLife/metalflight/blob/main/docs/BUILDING.md).

## Steam browser rendering

`MSFS24_WINE_STEAM_IN_PROCESS_GPU=1` adds Chromium's `--in-process-gpu` option
only to an executable whose basename is `steamwebhelper.exe` (case-insensitive).
The launcher enables it for Steam, alongside its separate D3D11 configuration.
The game does not receive this environment setting.

Wine updates the process's command line before kernelbase initializes its ANSI
and UTF-16 command-line caches. Direct Wine invocations and Windows-created
children use the same rule. Existing arguments remain unchanged, an existing
option is not duplicated, and an unset, zero or invalid setting has no effect.
The Steam helper retains its original name and contents; normal Steam update
and verification behavior remains available.

Authored positive and negative checks live in MetalFlight's
`probes/steam_launch_options.c` and `runtime/test_steam_options.py`. They run in a
new prefix with no Steam installation or account data. Readable Steam sign-in,
successful authentication and game readiness are separate validation outcomes.

## Hardware debug-register context cache (candidate)

`MSFS24_WINE_HW_CONTEXT_CACHE=1`, together with
`MSFS24_WINE_HW_DEBUG=1`, preserves this client's logical hardware execution
watches across thread suspension under Rosetta. The server cannot read a
translated process's native debug registers across the process boundary and can
return zeroes. Restoring those zeroes into the client cache while the real watch
remains armed makes a later hardware watch look like an ordinary breakpoint.
The ordinary breakpoint path then decrements the saved instruction pointer.

The candidate returns debug-only current-thread queries from the existing cache,
preserves the cache on suspension return, and supplies same-process peer debug
registers after the server's original context-access check. A bounded lookup
supports GET_CONTEXT-only handles without changing their access rights. Foreign
processes retain the existing server path. The switch is off unless explicitly
enabled, and has no effect without the Rosetta hardware-watch setting.

`MSFS24_WINE_TRAP_RESULTS=1` is a separate bounded observer. It records at most
32 matched and 32 unmatched trap metadata rows with both logical and native
debug registers. It reads no rendered images or network payloads. Leave it off
for ordinary launches.

Run the authored controls in a new directory with no Steam or game state:

```sh
python3 msfs24/test_context_cache.py --runtime /path/to/candidate-runtime \
    --cc /path/to/x86_64-w64-mingw32-clang --work /path/to/new-owned-on --cache on
python3 msfs24/test_context_cache.py --runtime /path/to/candidate-runtime \
    --cc /path/to/x86_64-w64-mingw32-clang --work /path/to/new-owned-off --cache off
```

The OFF arm expects the named readback and false-breakpoint defects; arbitrary
crashes, missing output and timeouts do not pass. The ON arm requires correct
readback and one hardware watch hit, with zero false breakpoints. The suite also
checks ordinary INT3 address/RIP delivery, application single-step ordering,
clear/replace behavior and context-access denial. Owned success does not by
itself establish a game's startup cause or repeated-launch reliability.
