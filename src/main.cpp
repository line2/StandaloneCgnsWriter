#include "CgnsWriter.h"

#include <vtkNew.h>
#include <vtkSmartPointer.h>

#include <vtkDataObject.h>
#include <vtkGenericDataObjectReader.h>
#include <vtkXMLGenericDataObjectReader.h>

#include <iostream>
#include <string>

namespace {
bool EndsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

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

void PrintUsage(const char *argv0) {
  std::cerr << "Usage:\n"
            << "  " << argv0
            << " <input.vtk|.vtu|.vtm|...> <output.cgns> [--adf|--hdf5] "
               "[--keep-ghost]\n\n"
            << "Notes:\n"
            << "  - This is a minimal standalone CGNS writer (no ParaView "
               "required).\n"
            << "  - Only a subset of unstructured cell types is supported "
               "(line/tri/quad/tet/pyr/wedge/hex).\n";
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string inputFile = argv[1];
  const std::string outputFile = argv[2];

  CgnsWriterOptions opt;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--adf") {
      opt.useHdf5 = false;
    } else if (a == "--hdf5") {
      opt.useHdf5 = true;
    } else if (a == "--keep-ghost") {
      opt.skipGhostCells = false;
    } else {
      std::cerr << "Unknown option: " << a << "\n";
      PrintUsage(argv[0]);
      return 2;
    }
  }

  try {
    vtkSmartPointer<vtkDataObject> obj = ReadAnyVtk(inputFile);
    if (!obj) {
      std::cerr << "Failed to read input: " << inputFile << "\n";
      return 1;
    }

    CgnsWriter::Write(obj, outputFile, opt);
    std::cout << "Wrote: " << outputFile << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
