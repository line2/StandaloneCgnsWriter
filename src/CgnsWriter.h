#pragma once

#include <string>

// Forward declare to keep this header light and not force VTK includes everywhere.
class vtkDataObject;

struct CgnsWriterOptions
{
  // Try to request HDF5 as the CGNS backend for newly created files.
  // Note: Whether this is honored depends on how your CGNS library was built.
  bool useHdf5 = true;

  // If true, ghost cells (VTK "vtkGhostType") will be skipped when writing unstructured elements.
  bool skipGhostCells = true;

  // CGNS requires 1-based indexing for connectivity.
  // This is always true, but exposed as an option to make the intent explicit.
  bool oneBasedConnectivity = true;

  // If true, write point-data arrays as Vertex-located FlowSolution.
  bool writePointData = true;

  // If true, write cell-data arrays as CellCenter-located FlowSolution.
  bool writeCellData = true;

  // Base name to use in the CGNS file.
  std::string baseName = "Base";

  // Zone name prefix. For composite inputs, zones become Zone0, Zone1, ...
  std::string zoneNamePrefix = "Zone";
};

class CgnsWriter
{
public:
  // Write a VTK data object (vtkDataSet or vtkCompositeDataSet) to a CGNS file.
  // Throws std::runtime_error on failure.
  static void Write(vtkDataObject* input, const std::string& fileName,
                    const CgnsWriterOptions& opt = CgnsWriterOptions{});
};
