# StandaloneCgnsWriter (VTK -> CGNS)

This is a small, standalone CMake C++ project that writes CGNS files from VTK datasets
(using **VTK** + **CGNS/cgnslib**). It **does not** depend on ParaView.

It is intentionally minimal and meant as a starting point for a fuller reimplementation
of ParaView's `vtkCGNSWriter`.

## Features (current)

- Accepts `vtkDataSet` and `vtkCompositeDataSet` (writes one Zone per leaf dataset).
- Structured datasets: `vtkImageData`, `vtkRectilinearGrid`, `vtkStructuredGrid`.
- Unstructured datasets: a subset of VTK cell types:
  - line, triangle, quad, tetra, pyramid, wedge, hexahedron
- Writes:
  - Coordinates (X/Y/Z)
  - Connectivity (Element sections)
  - PointData as `FlowSolution` at `Vertex`
  - CellData as `FlowSolution` at `CellCenter`
- Optionally skips ghost cells (`vtkGhostType`) when writing unstructured zones.

## Limitations (important)

- No polyhedron / NGON/NFACE support.
- No CGNS boundary condition nodes written.
- No “single file with multiple time steps” (iterative data) support.
- Vector/tensor arrays are written as separate scalar fields: `Name_X`, `Name_Y`, ...

## Build

You need VTK and CGNS installed.

Typical build:

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/vtk;/path/to/cgns"
cmake --build build -j
```

## Run

```bash
./build/vtk_to_cgns input.vtu output.cgns
./build/vtk_to_cgns input.vtm output.cgns --hdf5
./build/vtk_to_cgns input.vtk output.cgns --adf --keep-ghost
```
