# Packaging

How cuteNES becomes a download for each desktop OS, and why it is done this
way. The decisions are in `specs/003-qt-frontend/research.md` R13; asset
naming follows R16, the self-update design.

## The general approach

Any Qt desktop application packaged this way goes through the same three
steps:

1. **Install rules in CMake, in one component.** Everything the package
   holds (the executables, the license texts, the icon, desktop
   integration files) is installed under `COMPONENT app`. The component
   leaves out the install rules that libraries add for themselves (here the
   core's headers and static library, and GoogleTest's), so
   `cmake --install build --component app --prefix <stage>` produces the
   package and nothing else.
2. **Deploy Qt into the staged tree.** The program has to find Qt's
   libraries and plugins next to it, not on the build machine. On Windows
   and macOS, `qt_generate_deploy_app_script` runs Qt's own deployment tool
   (windeployqt or macdeployqt) as part of the install. On Linux the
   AppImage tools do it (step 3).
3. **Wrap, smoke-test, checksum.** The staged tree is zipped (Windows,
   macOS) or turned into an AppImage (Linux). The packaged program is then
   *run* on the build machine. A missing library or plugin fails there, in
   CI, instead of on a user's machine. Each file gets a SHA-256, and the
   run's checksums are gathered into one `SHA256SUMS`.

## Per platform

| OS | Package | Made by | Notes |
|---|---|---|---|
| Windows | `cuteNES-<version>-windows-x64.zip` | windeployqt (via the deploy script), `Compress-Archive` | Includes the MSVC runtime DLLs (`InstallRequiredSystemLibraries`), so no redistributable is needed. The executable's icon comes from `app/cutenes.rc.in`. |
| macOS | `cuteNES-<version>-macos-<arch>.zip` holding `cutenes.app` | macdeployqt (via the deploy script), `ditto` | Ad-hoc signed (`codesign --sign -`): there are no developer certificates for 1.0, so Gatekeeper asks on first open. `cutenes-server` is inside the bundle, in `Contents/MacOS`. |
| Linux | `cuteNES-<version>-linux-x86_64.AppImage` | linuxdeploy with its Qt plugin | Built on the oldest supported Ubuntu (22.04), because an AppImage runs only on systems with at least the glibc it was built with. It carries the `.desktop` entry and a MIME type for `.nes`. |

Every package carries `cutenes-server` (for MCP clients,
`cutenes-server --attach`) and the license texts that About shows: cuteNES's
own, Qt's LGPL-3.0 and GPL-3.0, SDL3's, nlohmann/json's and cpp-httplib's.

## When it runs

The `package` and `package-sums` jobs in `.github/workflows/build.yml` run
on tags (`v*`, `core-v*`), on the weekly scheduled run, and by hand
(`gh workflow run build.yml --ref <branch>`). They never run on pull
requests: packaging takes as long as a build and changes rarely, and the
weekly run catches drift in the runner images or tools. The packages are
workflow artifacts. Attaching them to a GitHub release is part of the
release steps (`docs/source-control.md`), which the maintainer approves.

## Building a package locally

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cutenes cutenes-server
cmake --install build --component app --prefix stage
# macOS: stage/cutenes.app; Windows: stage\cutenes.exe and its DLLs.
```

On Linux, install to `AppDir/usr` instead and run linuxdeploy as the CI
job does.

## The icon

`packaging/icons/cutenes.svg` is the source, drawn for this project.
`packaging/icons/make-icons.sh` (macOS, needs `rsvg-convert`) regenerates
the committed `cutenes.png` (512 px), `cutenes.icns` and `cutenes.ico` from
it. The window icon is the PNG, built in as a Qt resource.
