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
