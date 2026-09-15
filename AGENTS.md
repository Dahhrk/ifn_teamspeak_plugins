# Agents Î“Ã‡Ã¶ ifn_teamspeak_plugins

Private product. Kitchen is `dark-factory` Î“Ã‡Ã¶ do not invent product work there.

1. Start non-trivial work with `/poteto-mode`. Done means a checkable control CLI / UI / test result.
2. Prefer the project `control-*` CLI from `/create-verification-skill`. Maintain daily with `/maintain-verification-skill`.
3. One verifiable unit per PR. Author does not merge on own verdict.
4. Respect `.cursor/dune.md` and `BUGBOT.md`.
5. Never commit secrets. Never Autopilot until doctor/launch/drive evidence works.
6. Storage: kitchen `docs/storage-layout.md` + this repo `PRIVATE.md`.

## Repo gate

- This repo is public. Never commit IceFuse dumps, rank exports, credentials, or hardcoded live-server IDs - resolve channels and groups by name at runtime.
- Local gate: `powershell -ExecutionPolicy Bypass -File .\build.ps1` builds win32 + win64 and packs `dist/*.ts3_plugin`. Uses VS Build Tools 2022's bundled CMake, falls back to `cmake` on PATH.
- CI: `.github/workflows/build.yml` compiles both arches on windows-latest and opens every package to assert `package.ini` plus both DLLs.
- Done means for a plugin change: clean `.\build.ps1` AND the packed `.ts3_plugin` installs in a real TS3 client and the menu action behaves. The in-client step is manual - no scripted TS3 UI driver exists.
- Register plugins in `TS3_PLUGINS` in the root `CMakeLists.txt`. One folder per plugin: `plugins/<name>/CMakeLists.txt` with `ts3_plugin(<name> SOURCES src/<file>.cpp)`, a `package.ini`, and `src/`.
- Shared export boilerplate lives in `common/ts3plugin.hpp`. Plugins are single-TU DLLs; `static` helpers there are deliberate.
