# ifn_teamspeak_plugins

TeamSpeak 3 client plugins for IceFuse tasks. Windows (win32/win64), C++ against the official TS3 client Plugin SDK.

## Plugins

| Plugin | What it does |
|---|---|
| `group-finder` | Lists server groups under Plugins > IFN Group Finder. Pick one and it prints that group's members in the server chat tab - online members as clickable PM links, offline members in plain text. |
| `staff-tools` | Right-click a client: pull to your channel, send to Jail (indefinite or 5/15/30/60 min), timed Mute 10/30 min (Muted group), channel mute 10 min (Channel Muted group, restores prior channel group), Sticky (indefinite or 30 min), grant/revoke talk power, poke presets, kicks, 1h/24h bans. Timed actions auto-release - jail returns the client to their original channel and removes Sticky; mutes remove the group (works offline via database ID). A global "Jail/mute board" menu item lists active actions and time remaining. All channels and groups are resolved by name. |
| `staff-board` | Prints an online-staff board to the server tab, grouped by rank tier (Executive, Leadership, Moderation, TeamSpeak, Game Master, Mentors, Recruitment, Forums), with clickable PM links. |
| `gm-tools` | Event running: pull every online member of a group into your channel, poke everyone in your channel, grant/revoke talk power for the whole channel, server-wide event announcement, GM attendance radar (prints when Game Master group members connect/disconnect) and event-channel watch (prints when someone joins a channel matching the `WATCH_NEEDLES` list - "event" by default). Both toggleable. |
| `sticky-tools` | Standalone marking tool: right-click a user to apply the Sticky group (indefinite or 15/60 min with auto-removal, works offline via database ID), plus a global "Sticky board" listing marked users and time remaining. |

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
common/ts3plugin.hpp      shared export boilerplate, menu alloc, client/channel helpers
build.ps1                 one-shot configure/build/package for both arches
plugins/<name>/           one folder per plugin: CMakeLists.txt, package.ini, src/
dist/                     packaged .ts3_plugin output (gitignored)
```
