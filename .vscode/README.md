# VS Code development workflow

This directory provides an optional VS Code workflow for developing KeePassXC on Windows with MSVC, CMake Tools, vcpkg, and Qt 6.

KeePassXC's standard build and installation documentation remains authoritative. This configuration is intended to make the Windows development workflow convenient from VS Code.

## Prerequisites

Install and configure:

- Visual Studio with the C++ desktop development workload.
- CMake.
- VS Code.
- The CMake Tools extension for VS Code.
- The Microsoft C/C++ extension for VS Code.
- vcpkg in the repository's parent directory, using the layout described below.

The configuration in this repository has been tested with Visual Studio 18 2026, x64, CMake, vcpkg, and Qt 6.

## Expected directory layout

The VS Code settings use paths relative to the KeePassXC checkout:

```text
keepassxc-repo/
├── keepassxc/
│   └── .vscode/
└── vcpkg/
```

If the repository and vcpkg directories are located elsewhere, adjust the paths in `.vscode/settings.json`.

## Configure and build

Open the KeePassXC repository in VS Code. CMake Tools is configured to use:

- Generator: `Visual Studio 18 2026`
- Platform: `x64`
- Build directory: `build-vscode-x64`
- vcpkg toolchain from `../vcpkg/scripts/buildsystems/vcpkg.cmake`
- Qt 6 from the vcpkg installation
- `CMAKE_PROGRAM_PATH` pointing to the Qt 6 tools directory
- `KPXC_FEATURE_DOCS=OFF`

The documentation build is disabled because the offline documentation requires the Asciidoctor executable. This does not disable KeePassXC's application functionality.

CMake Tools can configure the project automatically when the folder is opened. To build from VS Code, use **Terminal > Run Build Task** or the CMake Tools build command. The committed `tasks.json` provides the default task named **CMake: Build KeePassXC**.

The equivalent PowerShell build command is:

```powershell
cmake --build .\build-vscode-x64 --config Debug -j 4
```

The Debug executable is produced at:

```text
build-vscode-x64\src\Debug\KeePassXC.exe
```

## Debug with F5

The committed `launch.json` uses CMake Tools to resolve the selected launch target, so the executable path does not need to be hard-coded.

1. Select the **Debug** configuration in CMake Tools.
2. Select **KeePassXC** as the launch target if necessary.
3. Press **F5**.

The debugger uses Visual Studio's C++ debugger (`cppvsdbg`).

## Command-line fallback

If VS Code or CMake Tools needs troubleshooting, the same configuration can be reproduced from PowerShell:

```powershell
Remove-Item -Recurse -Force .\build-vscode-x64

cmake -S . -B .\build-vscode-x64 `
  -G "Visual Studio 18 2026" `
  -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:\Users\<username>\Personal_Coding\keepassxc-repo\vcpkg\scripts\buildsystems\vcpkg.cmake" `
  -DQt6_DIR="C:\Users\<username>\Personal_Coding\keepassxc-repo\vcpkg\installed\x64-windows\share\Qt6" `
  -DCMAKE_PROGRAM_PATH="C:\Users\<username>\Personal_Coding\keepassxc-repo\vcpkg\installed\x64-windows\tools\Qt6\bin" `
  -DKPXC_FEATURE_DOCS=OFF

cmake --build .\build-vscode-x64 --config Debug -j 4
```

Replace `<username>` and the parent directory with the locations used on the development machine.

## CMake presets

This checkout does not contain `CMakePresets.json`. Therefore commands such as:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
```

are not applicable to this checkout. The VS Code configuration intentionally uses explicit CMake Tools settings instead.

## Troubleshooting Qt and windeployqt

KeePassXC's CMake configuration locates `windeployqt` through the Qt installation. If CMake cannot find it, verify that the Qt tools directory from vcpkg contains:

```text
windeployqt.exe
```

The committed settings supply that directory through `CMAKE_PROGRAM_PATH`.

## Files in this directory

- `settings.json` — CMake Tools configuration for the Windows/MSVC build.
- `tasks.json` — default VS Code CMake build task.
- `launch.json` — F5 configuration using the CMake Tools launch target.
- `README.md` — this workflow documentation.

Do not commit generated build output such as `build-vscode-x64/`.
