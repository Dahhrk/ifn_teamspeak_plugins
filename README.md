# ifn_teamspeak_plugins

TeamSpeak 3 client plugins for IceFuse tasks. Windows (win32/win64), C++ against the official TS3 client Plugin SDK.

## Plugins

| Plugin | What it does |
|---|---|
| `group-finder` | Lists server groups under Plugins > IFN Group Finder. Pick one and it prints that group's members in the server chat tab - online members as clickable PM links, offline members in plain text. |

## Build

Requires Visual Studio Build Tools 2022 (MSVC + bundled CMake). From the repo root:

```powershell
.\build.ps1            # both arches
.\build.ps1 -Arch win64
```

Output lands in `dist/<name>.ts3_plugin`. Run the file to install, or drop it on the TeamSpeak client.

## Adding a plugin

1. `mkdir plugins/<name>/src` and add a `CMakeLists.txt` containing `ts3_plugin(<name> SOURCES src/<file>.cpp)`.
2. Write a `package.ini` in `plugins/<name>/`.
3. Add `<name>` to `TS3_PLUGINS` in the root `CMakeLists.txt`.

The build, staging, and `.ts3_plugin` packaging are derived from the registry entry.

## Layout

```text
CMakeLists.txt            root build, SDK pin, plugin registry
cmake/Ts3Plugin.cmake     ts3_plugin() helper: shared lib + .ts3_plugin packaging
build.ps1                 one-shot configure/build/package for both arches
plugins/<name>/           one folder per plugin: CMakeLists.txt, package.ini, src/
dist/                     packaged .ts3_plugin output (gitignored)
```
