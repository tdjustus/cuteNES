# cuteNES

[![build](https://github.com/tdjustus/cuteNES/actions/workflows/build.yml/badge.svg)](https://github.com/tdjustus/cuteNES/actions/workflows/build.yml)

cuteNES is a free, open-source NES emulator for macOS, Windows and Linux. It
aims to play games exactly as the console does, and it has save states,
rewind, cheats, movies and debugging tools. An AI agent can connect to it
over MCP and play or inspect a game too.

cuteNES is in testing: the current releases are alphas. Everything in the
menus works, and what is known not to is listed with each release and in
the [1.0 milestone](https://github.com/tdjustus/cuteNES/milestone/1).
What comes after 1.0 is in the [roadmap](docs/roadmap.md).

## Download

Get the latest release from
[GitHub Releases](https://github.com/tdjustus/cuteNES/releases/latest):

| File | For |
|---|---|
| `cuteNES-<version>-macos-arm64.zip` | macOS 13 or later, Apple silicon |
| `cuteNES-<version>-windows-x64.zip` | Windows 10 or later, 64-bit |
| `cuteNES-<version>-linux-x86_64.AppImage` | Linux x86-64 with glibc 2.35 or later (Ubuntu 22.04 and newer); needs `libfuse2` |

The builds are not signed by a publisher, so the first start needs one
extra step:

- **macOS:** unzip, then right-click **cutenes.app**, choose **Open** and
  confirm. Or run `xattr -dr com.apple.quarantine cutenes.app`.
- **Windows:** unzip the whole folder anywhere and run `cutenes.exe`. If
  SmartScreen warns, choose **More info**, then **Run anyway**.
- **Linux:** `chmod +x cuteNES-<version>-linux-x86_64.AppImage`, then run
  it.

Each release also has a `SHA256SUMS` file: `shasum -a 256 -c SHA256SUMS`
checks your download.

## Getting started

1. **Start cuteNES.** The first time, it makes a library folder,
   `Documents/cuteNES`, for your games, saves and screenshots.
2. **Add your games.** Put `.nes` files in the library's `roms` folder, or
   click **Add Folder…** to use a folder you already have. cuteNES never
   changes or moves your ROM files.
3. **Pick a game** and press Enter, or double-click it. You can also drag a
   `.nes` file onto the window.

cuteNES comes with no games. Play your own copies of games you own, or
homebrew and public-domain games.

## Controls

| | Keyboard | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad or left stick |
| A / B | X / Z | South / West (Xbox A / X) |
| Select / Start | Right Shift / Enter | Back / Start |
| Pause | P | |
| Save / load state | Shift+F1-F10 / F1-F10 | |
| Fast forward / rewind | Tab / Backspace (held) | |

On macOS, read Ctrl as Cmd in the menus. Every key and gamepad button can be
changed on the Key Bindings page (**Help > Keyboard Shortcuts**).

## Learn more

The [user guide](docs/user-guide.md), also under **Help > User Guide**,
covers everything else:

- battery saves and save states, and where cuteNES keeps your files;
- the picture and sound settings;
- cheats, including a live search for values in a running game;
- recording and playing back movies;
- the debugger, memory viewer, PPU viewers and trace logger;
- connecting an AI agent;
- troubleshooting.

## Reporting a problem

Report bugs on the [issue tracker](https://github.com/tdjustus/cuteNES/issues).
**Help > Open Log Folder** shows cuteNES's log; attaching it helps. The log
holds file paths (the games you opened) but never your server tokens, so
look it over before sharing it.

## For developers

cuteNES is built on a headless, deterministic C++20 emulation core: the same
ROM and inputs give byte-identical video and audio on every platform, and
the core is checked against the community's hardware test ROMs.

- [Documentation index](docs/README.md)
- [Building and testing](docs/building-and-testing.md)
- [Contributing](docs/contributing.md) and the
  [development process](docs/development-process.md)
- [Automation: MCP and the API](docs/automation.md)
- [Using the core in your own program](docs/using-the-core.md)
- [Source control and releases](docs/source-control.md)
- [Roadmap](docs/roadmap.md)
- [Design philosophy](docs/design-philosophy.md) and [architecture](docs/architecture.md)

## Legal use

cuteNES is for playing games you own and for homebrew. The project does not
condone piracy and is designed not to enable it:

- It ships **no commercial ROMs**, and never will. The test suite uses
  freely licensed test ROMs; tests on commercial games run only on a
  developer's own copies in the gitignored `roms-local/`.
- Play your own dumps of cartridges you own, or homebrew and public-domain
  games. Do not download or share ROMs you have no right to.
- Features that touch other people's content are built the same way: game
  art (#90) uses only sources that allow it, and the planned support for
  ROM hacks (#219) applies a hack's patch to your own copy of the game, in
  memory, so no ROM is ever shipped with one.

## License

cuteNES is MIT-licensed (see `LICENSE`). Its packages bundle Qt 6 under the
LGPL-3.0 (dynamically linked) and SDL 3 under the zlib license; the texts
are in Help > About. Test-ROM provenance is in
[`third_party/NOTICE.md`](third_party/NOTICE.md).
