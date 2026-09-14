# ifn_teamspeak_plugins - agent notes

TeamSpeak 3 client plugins for IceFuse. Windows-only C++ (MSVC) against the
official client Plugin SDK. Public repo - never commit IceFuse dumps, rank
exports, credentials, or hardcoded live-server IDs.

## Gate

- Local: `powershell -ExecutionPolicy Bypass -File .\build.ps1` builds win32 +
  win64 and packs `dist/*.ts3_plugin`. Expects VS Build Tools 2022's bundled
  CMake; falls back to `cmake` on PATH.
- CI: `.github/workflows/build.yml` runs the same both-arch build on
  windows-latest and opens every package to verify `package.ini` plus both
  arch DLLs.
- Done means for a plugin change: clean `.\build.ps1` AND the packed
  `.ts3_plugin` installs in a real TS3 client and the menu action behaves.
  The in-client step is manual - there is no scripted UI driver for TS3.
- `.devin` blueprint intentionally absent: MSVC-only build, cloud sessions
  cannot compile or verify this repo.

## Conventions

- Register plugins in `TS3_PLUGINS` in the root `CMakeLists.txt`. One folder
  per plugin: `plugins/<name>/CMakeLists.txt` containing
  `ts3_plugin(<name> SOURCES src/<file>.cpp)`, a `package.ini`, and `src/`.
- Shared export boilerplate lives in `common/ts3plugin.hpp`:
  `TS3_PLUGIN_IDENTITY`, `ts3MakeMenuItem`, `ts3Sanitize`, `ts3ClientList`,
  `ts3ClientString`, `ts3FindChannelByName`, `ts3SelfClientID`.
- Resolve live-server objects by name at runtime, not by ID - see the jail
  channel lookup in staff-tools. Rank-name matching (staff-board
  `STAFF_TIERS`) is acceptable because group names are stable and the table
  is the editable surface.
- API calls that stream results use a `returnCode` string per plugin and are
  flushed in `ts3plugin_onServerErrorEvent` when `error == ERROR_ok`.
- Plugins are single-TU DLLs; `static` helpers in the shared header are
  deliberate.
