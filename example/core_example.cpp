#include "CgnsWriterCore.h"
#include "CgnsWriterExport.h"


#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <stdexcept>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkGenericDataObjectReader.h>
#include <vtkIdList.h>
#include <vtkNew.h>
#include <vtkPointSet.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>
#include <vtkXMLGenericDataObjectReader.h>


// VTK cell type constants (since we don't depend on VTK)
namespace {
constexpr unsigned char VTK_VERTEX = 1;
constexpr unsigned char VTK_LINE = 3;
constexpr unsigned char VTK_TRIANGLE = 5;
constexpr unsigned char VTK_QUAD = 9;
constexpr unsigned char VTK_TETRA = 10;
constexpr unsigned char VTK_HEXAHEDRON = 12;
constexpr unsigned char VTK_WEDGE = 13;
constexpr unsigned char VTK_PYRAMID = 14;

// Mesh data structure to hold temporary mesh data
struct MeshData {
  std::vector<double> points;
  std::vector<int32_t> connectivity_32;
  std::vector<int64_t> connectivity_64;
  std::vector<int32_t> offsets_32;
  std::vector<int64_t> offsets_64;
  std::vector<unsigned char> types;
  int64_t num_points = 0;
  int64_t num_cells = 0;
  bool use_64bit = true;
};

// Convert MeshData to UnstructuredMeshInfo
UnstructuredMeshInfo MeshDataToInfo(const MeshData &mesh) {
  UnstructuredMeshInfo info = {};
  info.points = const_cast<double *>(mesh.points.data());
  info.num_points = mesh.num_points;
  info.num_cells = mesh.num_cells;
  info.types = const_cast<unsigned char *>(mesh.types.data());
  info.use_64bit_ids = mesh.use_64bit ? 1 : 0;

  if (mesh.use_64bit) {
    info.connectivity = const_cast<void *>(
        static_cast<const void *>(mesh.connectivity_64.data()));
    info.connectivity_size = static_cast<int64_t>(mesh.connectivity_64.size());
    info.offsets =
        const_cast<void *>(static_cast<const void *>(mesh.offsets_64.data()));
  } else {
    info.connectivity = const_cast<void *>(
        static_cast<const void *>(mesh.connectivity_32.data()));
    info.connectivity_size = static_cast<int64_t>(mesh.connectivity_32.size());
    info.offsets =
        const_cast<void *>(static_cast<const void *>(mesh.offsets_32.data()));
  }

  return info;
}

// Helper function to check if a string ends with a suffix
bool EndsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Read VTK file using appropriate reader
vtkSmartPointer<vtkDataObject> ReadAnyVtk(const std::string &fileName) {
  // Very simple heuristic: use XML reader for common XML VTK extensions.
  const bool isXml =
      EndsWith(fileName, ".vtu") || EndsWith(fileName, ".vtp") ||
      EndsWith(fileName, ".vts") || EndsWith(fileName, ".vtr") ||
      EndsWith(fileName, ".vti") || EndsWith(fileName, ".vtm") ||
      EndsWith(fileName, ".vth") || EndsWith(fileName, ".vtd") ||
      EndsWith(fileName, ".vtpc") || EndsWith(fileName, ".vtx") ||
      EndsWith(fileName, ".pvtu") || EndsWith(fileName, ".pvtp") ||
      EndsWith(fileName, ".pvts") || EndsWith(fileName, ".pvtr") ||
      EndsWith(fileName, ".pvti") || EndsWith(fileName, ".pvd");

  if (isXml) {
    vtkNew<vtkXMLGenericDataObjectReader> r;
    r->SetFileName(fileName.c_str());
    r->Update();
    return r->GetOutput();
  } else {
    vtkNew<vtkGenericDataObjectReader> r;
    r->SetFileName(fileName.c_str());
    r->Update();
    return r->GetOutput();
  }
}

// Convert VTK DataObject to MeshData
MeshData VtkToMeshData(vtkDataObject *obj, bool use64bit = true,
                       bool skipGhostCells = true) {
  MeshData mesh;
  mesh.use_64bit = use64bit;

  vtkDataSet *ds = vtkDataSet::SafeDownCast(obj);
  if (!ds) {
    throw std::runtime_error("Input is not a vtkDataSet");
  }

  // Get ghost cell array if needed
  vtkUnsignedCharArray *ghost = nullptr;
  if (skipGhostCells) {
    vtkCellData *cd = ds->GetCellData();
    if (cd) {
      ghost = vtkUnsignedCharArray::SafeDownCast(cd->GetArray("vtkGhostType"));
    }
  }

  // Extract points
  vtkPointSet *ps = vtkPointSet::SafeDownCast(ds);
  if (!ps) {
    throw std::runtime_error(
        "Input is not a vtkPointSet (cannot extract points)");
  }

  vtkPoints *points = ps->GetPoints();
  if (!points) {
    throw std::runtime_error("Dataset has no points");
  }

  vtkIdType numPoints = points->GetNumberOfPoints();
  mesh.num_points = static_cast<int64_t>(numPoints);
  mesh.points.resize(static_cast<size_t>(numPoints * 3));

  for (vtkIdType i = 0; i < numPoints; ++i) {
    double p[3];
    points->GetPoint(i, p);
    const size_t idx = static_cast<size_t>(i * 3);
    mesh.points[idx] = p[0];
    mesh.points[idx + 1] = p[1];
    mesh.points[idx + 2] = p[2];
  }

  // Extract cells
  vtkIdType numCells = ds->GetNumberOfCells();
  vtkNew<vtkIdList> ptIds;

  // First pass: count valid cells and collect connectivity
  std::vector<int64_t> connectivity_64;
  std::vector<int32_t> connectivity_32;
  std::vector<int64_t> offsets_64;
  std::vector<int32_t> offsets_32;
  std::vector<unsigned char> types;

  int64_t offset = 0;
  int64_t cellCount = 0;

  for (vtkIdType cid = 0; cid < numCells; ++cid) {
    // Skip ghost cells if requested
    if (ghost && ghost->GetNumberOfTuples() == numCells) {
      const unsigned char g = ghost->GetValue(cid);
      if (g != 0) {
        continue;
      }
    }

    const int vtkType = ds->GetCellType(cid);

    // Map VTK cell type to our supported types
    // Only support: VTK_VERTEX=1, VTK_LINE=3, VTK_TRIANGLE=5, VTK_QUAD=9,
    //               VTK_TETRA=10, VTK_HEXAHEDRON=12, VTK_WEDGE=13,
    //               VTK_PYRAMID=14
    if (vtkType != VTK_VERTEX && vtkType != VTK_LINE &&
        vtkType != VTK_TRIANGLE && vtkType != VTK_QUAD &&
        vtkType != VTK_TETRA && vtkType != VTK_HEXAHEDRON &&
        vtkType != VTK_WEDGE && vtkType != VTK_PYRAMID) {
      // Skip unsupported cell types
      continue;
    }

    ds->GetCellPoints(cid, ptIds);
    const vtkIdType numPts = ptIds->GetNumberOfIds();

    if (use64bit) {
      offsets_64.push_back(offset);
      for (vtkIdType pi = 0; pi < numPts; ++pi) {
        connectivity_64.push_back(static_cast<int64_t>(ptIds->GetId(pi)));
      }
      offset += numPts;
    } else {
      offsets_32.push_back(static_cast<int32_t>(offset));
      for (vtkIdType pi = 0; pi < numPts; ++pi) {
        connectivity_32.push_back(static_cast<int32_t>(ptIds->GetId(pi)));
      }
      offset += numPts;
    }

    types.push_back(static_cast<unsigned char>(vtkType));
    cellCount++;
  }

  // Add final offset
  if (use64bit) {
    offsets_64.push_back(offset);
    mesh.connectivity_64 = std::move(connectivity_64);
    mesh.offsets_64 = std::move(offsets_64);
  } else {
    offsets_32.push_back(static_cast<int32_t>(offset));
    mesh.connectivity_32 = std::move(connectivity_32);
    mesh.offsets_32 = std::move(offsets_32);
  }

  mesh.types = std::move(types);
  mesh.num_cells = cellCount;

  return mesh;
}

} // namespace

void PrintUsage(const char *programName) {
  std::cerr << "Usage: " << programName << " [options] <input.vtk|.vtu|...> <output.cgns>\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  --api <c|cpp|both>        API to use (default: both)\n";
  std::cerr << "  --format <hdf5|adf>      File format (default: hdf5)\n";
  std::cerr << "  --32bit                   Use 32-bit indices\n";
  std::cerr << "  --64bit                   Use 64-bit indices (default)\n";
  std::cerr << "  --base-name <name>        Custom base name\n";
  std::cerr << "  --zone-name <name>        Custom zone name\n";
  std::cerr << "  --keep-ghost              Keep ghost cells\n";
  std::cerr << "  --version                 Show version information\n";
  std::cerr << "  --help                    Show this help message\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  " << programName << " input.vtu output.cgns\n";
  std::cerr << "  " << programName
            << " --api c --format adf input.vtk output.cgns\n";
  std::cerr << "  " << programName
            << " --32bit --base-name MyBase input.vtu output.cgns\n";
}

// Example using C API
int ExampleCAPI(const MeshData &mesh, const char *outputPath,
                const CgnsWriteOptions *options) {
  std::cout << "\n=== C API Example ===\n";
  std::cout << "Writing mesh with " << mesh.num_points << " points and "
            << mesh.num_cells << " cells...\n";

  UnstructuredMeshInfo info = MeshDataToInfo(mesh);

  int result = cgns_write_unstructured(&info, outputPath, options);

  if (result == 0) {
    std::cout << "Successfully wrote: " << outputPath << "\n";
  } else {
    const char *err = cgns_get_last_error();
    std::cerr << "Error writing file: " << (err ? err : "Unknown error")
              << "\n";
  }

  return result;
}

// Example using C++ API
int ExampleCppAPI(const MeshData &mesh, const char *outputPath,
                  const CgnsWriteOptions *options) {
  std::cout << "\n=== C++ API Example ===\n";
  std::cout << "Writing mesh with " << mesh.num_points << " points and "
            << mesh.num_cells << " cells...\n";

  UnstructuredMeshInfo info = MeshDataToInfo(mesh);

  int result = cgns_writer::WriteUnstructured(info, outputPath, options);

  if (result == 0) {
    std::cout << "Successfully wrote: " << outputPath << "\n";
  } else {
    const char *err = cgns_get_last_error();
    std::cerr << "Error writing file: " << (err ? err : "Unknown error")
              << "\n";
  }

  return result;
}

// Example demonstrating error handling
void ExampleErrorHandling() {
  std::cout << "\n=== Error Handling Example ===\n";

  UnstructuredMeshInfo invalidMesh = {};
  invalidMesh.points = nullptr;
  invalidMesh.num_points = 0;

  int result = cgns_write_unstructured(&invalidMesh, "invalid.cgns", nullptr);

  if (result != 0) {
    const char *err = cgns_get_last_error();
    std::cout << "Expected error caught: " << (err ? err : "Unknown error")
              << "\n";
  }
}

// Example demonstrating version information
void ExampleVersionInfo() {
  std::cout << "\n=== Version Information ===\n";
  const char *version = cgns_writer_version();
  std::cout << "CGNS Writer version: " << (version ? version : "Unknown")
            << "\n";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  // Default options
  std::string apiType = "both";
  std::string format = "hdf5";
  bool use64bit = true;
  bool skipGhostCells = true;
  std::string baseName;
  std::string zoneName;
  std::string inputPath;
  std::string outputPath;

  // Parse arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg == "--version") {
      ExampleVersionInfo();
      return 0;
    } else if (arg == "--api" && i + 1 < argc) {
      apiType = argv[++i];
    } else if (arg == "--format" && i + 1 < argc) {
      format = argv[++i];
    } else if (arg == "--32bit") {
      use64bit = false;
    } else if (arg == "--64bit") {
      use64bit = true;
    } else if (arg == "--keep-ghost") {
      skipGhostCells = false;
    } else if (arg == "--base-name" && i + 1 < argc) {
      baseName = argv[++i];
    } else if (arg == "--zone-name" && i + 1 < argc) {
      zoneName = argv[++i];
    } else if (arg[0] != '-') {
      if (inputPath.empty()) {
        inputPath = arg;
      } else if (outputPath.empty()) {
        outputPath = arg;
      } else {
        std::cerr << "Error: Too many file arguments\n";
        PrintUsage(argv[0]);
        return 1;
      }
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (inputPath.empty()) {
    std::cerr << "Error: Input file path is required\n";
    PrintUsage(argv[0]);
    return 1;
  }

  if (outputPath.empty()) {
    std::cerr << "Error: Output file path is required\n";
    PrintUsage(argv[0]);
    return 1;
  }

  // Read mesh from input file
  MeshData mesh;
  try {
    vtkSmartPointer<vtkDataObject> obj = ReadAnyVtk(inputPath);
    if (!obj) {
      std::cerr << "Failed to read input: " << inputPath << "\n";
      return 1;
    }

    // Convert VTK to MeshData
    mesh = VtkToMeshData(obj, use64bit, skipGhostCells);
  } catch (const std::exception &e) {
    std::cerr << "Error reading input file: " << e.what() << "\n";
    return 1;
  }

  // Prepare options
  CgnsWriteOptions options = {};
  options.use_hdf5 = (format == "hdf5") ? 1 : 0;
  options.base_name = baseName.empty() ? nullptr : baseName.c_str();
  options.zone_name = zoneName.empty() ? nullptr : zoneName.c_str();

  // Show version info
  ExampleVersionInfo();

  // Show mesh info
  std::cout << "\n=== Mesh Information ===\n";
  std::cout << "Points: " << mesh.num_points << "\n";
  std::cout << "Cells: " << mesh.num_cells << "\n";
  std::cout << "Index size: " << (use64bit ? "64-bit" : "32-bit") << "\n";
  std::cout << "Format: " << format << "\n";
  if (!baseName.empty()) {
    std::cout << "Base name: " << baseName << "\n";
  }
  if (!zoneName.empty()) {
    std::cout << "Zone name: " << zoneName << "\n";
  }

  // Run examples based on API type
  int result = 0;

  if (apiType == "c" || apiType == "both") {
    std::string cOutputPath = outputPath;
    if (apiType == "both") {
      // Add suffix for C API output
      size_t dotPos = outputPath.find_last_of('.');
      if (dotPos != std::string::npos) {
        cOutputPath =
            outputPath.substr(0, dotPos) + "_c" + outputPath.substr(dotPos);
      } else {
        cOutputPath = outputPath + "_c";
      }
    }

    result = ExampleCAPI(mesh, cOutputPath.c_str(), &options);
    if (result != 0) {
      return result;
    }
  }

  if (apiType == "cpp" || apiType == "both") {
    std::string cppOutputPath = outputPath;
    if (apiType == "both") {
      // Add suffix for C++ API output
      size_t dotPos = outputPath.find_last_of('.');
      if (dotPos != std::string::npos) {
        cppOutputPath =
            outputPath.substr(0, dotPos) + "_cpp" + outputPath.substr(dotPos);
      } else {
        cppOutputPath = outputPath + "_cpp";
      }
    }

    result = ExampleCppAPI(mesh, cppOutputPath.c_str(), &options);
    if (result != 0) {
      return result;
    }
  }

  // Demonstrate error handling
  ExampleErrorHandling();

  std::cout << "\n=== Example completed successfully ===\n";
  return 0;
}
