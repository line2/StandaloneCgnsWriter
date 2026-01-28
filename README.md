# StandaloneCgnsWriter (VTK → CGNS)

StandaloneCgnsWriter is a minimal C++/CMake project that exports VTK datasets (structured or
unstructured) into CGNS files. It targets the same problem space as ParaView's `vtkCGNSWriter`
but keeps the dependency graph small: only VTK and CGNS/libcgns are required—no ParaView
runtime.

## CMake configuration

- **CMake version**: requires 3.20 or later, enables `CMAKE_EXPORT_COMPILE_COMMANDS`, and forces
  `CMAKE_CXX_STANDARD` to 17 with `CMAKE_STANDARD_REQUIRED`.
- **Dependencies**: locates VTK modules (`CommonCore`, `CommonDataModel`,
  `CommonExecutionModel`, `IOLegacy`, `IOXML`) via `find_package`. CGNS is expected to be
  available via a config package (`cgns CONFIG REQUIRED`), and there is a historical fallback
  path that can activate a `FindCGNS.cmake` in `cmake/` if needed.
- **Targets**:
  - `cgns_writer`: header-only interface placed under `src/` and linked against either
    `CGNS::cgns_shared` or `CGNS::cgns_static` plus the core VTK libs.
  - `cgns_writer_dll`: optional shared library (`BUILD_CGNS_DLL` ON by default) that builds
    the standalone CGNS-only API, exports symbols with `CgnsWriterExport.h`, and installs
    binaries/headers under `bin/lib/include`.

## CMake presets

- **`default`** (`Ninja`, `Release`): builds in `${sourceDir}/build`, pulls VTK/CGNS out of
  `${sourceDir}/install/default`, and relies on the vcpkg toolchain (`$VCPKG_ROOT` must be set)
  with the `x64-windows` triplet.
- **`debug-dll`** / **`release-dll`**: similar to `default` but switch to `build-debug` or
  `build-release-dll`, enable `BUILD_CGNS_DLL`, and select either `Debug` or `Release`.
- **`vs-2022`**: targets Visual Studio 2022 (`x64`) with the same vcpkg configuration.
  Paired `build` presets (`vs-debug`, `vs-release`) pick the MSVC configuration.
- All presets share the vcpkg toolchain file (`scripts/buildsystems/vcpkg.cmake`) and place
  install artifacts under `${sourceDir}/install/${presetName}` so each configuration can have
  its own `VCPKG_INSTALLED_DIR`.

## Working with CMake presets

```bash
cmake --preset default               # configures the Release/Ninja build
cmake --build --preset default       # builds the default target
cmake --preset debug-dll             # configure debug DLL
cmake --build --preset debug-dll     # rebuilds with BUILD_CGNS_DLL=ON
cmake --preset vs-2022               # configure MSVC solution
cmake --build --preset vs-release     # compiles the release configuration
```

Ensure `VCPKG_ROOT` is defined so each preset can find `vcpkg.cmake`; the presets expect
`install/<presetName>` to hold the generated `installed` tree.

