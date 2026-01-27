#include "CgnsWriter.h"

#include <cgnslib.h>

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <chrono>
#include <fstream>
#include <sstream>

// VTK
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetAttributes.h>
#include <vtkIdList.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkPoints.h>
#include <vtkRectilinearGrid.h>
#include <vtkSmartPointer.h>
#include <vtkStructuredGrid.h>
#include <vtkUnsignedCharArray.h>

namespace
{
int64_t NowMs()
{
  using clock = std::chrono::system_clock;
  const auto now = clock::now().time_since_epoch();
  return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string EscapeJson(const std::string& s)
{
  std::string out;
  out.reserve(s.size());
  for (char ch : s)
  {
    if (ch == '\\' || ch == '"')
    {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

void DebugLog(const char* location, const char* message, const std::string& dataJson, const char* hypothesisId)
{
  std::ofstream out("c:\\Users\\line2\\project\\StandaloneCgnsWriter\\.cursor\\debug.log", std::ios::app);
  if (!out)
  {
    return;
  }
  out << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"" << hypothesisId
      << "\",\"location\":\"" << location << "\",\"message\":\"" << message << "\",\"data\":"
      << dataJson << ",\"timestamp\":" << NowMs() << "}\n";
}

void CheckCg(const int ierr, const std::string& what)
{
  if (ierr == CG_OK)
  {
    return;
  }
  const char* msg = cg_get_error();
  std::string err = msg ? msg : "Unknown CGNS error";
  throw std::runtime_error(what + ": " + err);
}

int InferPhysicalDim(vtkDataSet* ds)
{
  // VTK point coordinates are typically 3D even for 2D meshes.
  // We'll keep physDim=3 unless the points have fewer components.
  vtkPointSet* ps = ds ? vtkPointSet::SafeDownCast(ds) : nullptr;
  vtkPoints* pts = ps ? ps->GetPoints() : nullptr;
  const int comps = (pts && pts->GetData()) ? pts->GetData()->GetNumberOfComponents() : 0;
  // #region agent log
  {
    const std::string cls = ds ? EscapeJson(ds->GetClassName()) : "null";
    std::ostringstream data;
    data << "{\"dsClass\":\"" << cls << "\",\"hasPointSet\":" << (ps ? 1 : 0)
         << ",\"hasPointsData\":" << ((pts && pts->GetData()) ? 1 : 0) << ",\"components\":" << comps << "}";
    DebugLog("CgnsWriter.cpp:InferPhysicalDim", "points-metadata", data.str(), "H1");
  }
  // #endregion
  if (!ds || !pts || !pts->GetData())
  {
    return 3;
  }
  return (comps >= 3) ? 3 : std::max(1, comps);
}

int InferCellDim(vtkDataSet* ds)
{
  if (!ds)
  {
    return 3;
  }
  int cellDim = 0;
  const vtkIdType nCells = ds->GetNumberOfCells();
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    vtkCell* cell = ds->GetCell(cid);
    if (cell)
    {
      cellDim = std::max(cellDim, cell->GetCellDimension());
    }
  }
  // If the dataset is empty, default to 3 (most CFD/FEA use cases).
  return (cellDim > 0) ? cellDim : 3;
}

struct ZoneInput
{
  vtkSmartPointer<vtkDataSet> ds;
  std::string zoneName;
};

std::vector<ZoneInput> FlattenToZones(vtkDataObject* input, const CgnsWriterOptions& opt)
{
  std::vector<ZoneInput> zones;
  if (!input)
  {
    return zones;
  }

  if (auto* ds = vtkDataSet::SafeDownCast(input))
  {
    zones.push_back(ZoneInput{ ds, opt.zoneNamePrefix + std::string("0") });
    return zones;
  }

  if (auto* cd = vtkCompositeDataSet::SafeDownCast(input))
  {
    vtkSmartPointer<vtkCompositeDataIterator> it;
    it.TakeReference(cd->NewIterator());
    it->SkipEmptyNodesOn();

    int zi = 0;
    for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem())
    {
      vtkDataObject* cur = it->GetCurrentDataObject();
      auto* curDs = vtkDataSet::SafeDownCast(cur);
      if (!curDs)
      {
        continue;
      }
      zones.push_back(ZoneInput{ curDs, opt.zoneNamePrefix + std::to_string(zi++) });
    }
  }

  return zones;
}

struct Coords
{
  std::vector<double> x, y, z;
};

// Expand coordinates for structured grids into flat arrays (i-fastest, then j, then k)
Coords GetStructuredCoords(vtkDataSet* ds, const int dims[3], const int physDim)
{
  const vtkIdType npts = static_cast<vtkIdType>(dims[0]) * dims[1] * dims[2];
  Coords c;
  c.x.resize(npts);
  c.y.resize(npts);
  c.z.resize(npts);

  if (auto* rg = vtkRectilinearGrid::SafeDownCast(ds))
  {
    // #region agent log
    {
      std::ostringstream data;
      data << "{\"branch\":\"rectilinear\",\"dims\":[" << dims[0] << "," << dims[1] << "," << dims[2] << "]}";
      DebugLog("CgnsWriter.cpp:GetStructuredCoords", "branch-selected", data.str(), "H2");
    }
    // #endregion
    vtkDataArray* xa = rg->GetXCoordinates();
    vtkDataArray* ya = rg->GetYCoordinates();
    vtkDataArray* za = rg->GetZCoordinates();

    for (int k = 0; k < dims[2]; ++k)
    {
      const double z = (physDim >= 3 && za) ? za->GetComponent(k, 0) : 0.0;
      for (int j = 0; j < dims[1]; ++j)
      {
        const double y = (physDim >= 2 && ya) ? ya->GetComponent(j, 0) : 0.0;
        for (int i = 0; i < dims[0]; ++i)
        {
          const double x = xa ? xa->GetComponent(i, 0) : 0.0;
          const vtkIdType idx = static_cast<vtkIdType>(i + dims[0] * (j + dims[1] * k));
          c.x[idx] = x;
          c.y[idx] = y;
          c.z[idx] = z;
        }
      }
    }
    return c;
  }

  // vtkImageData / vtkStructuredGrid: just read points in VTK order
  vtkPointSet* ps = ds ? vtkPointSet::SafeDownCast(ds) : nullptr;
  vtkPoints* pts = ps ? ps->GetPoints() : nullptr;
  // #region agent log
  {
    const std::string cls = ds ? EscapeJson(ds->GetClassName()) : "null";
    std::ostringstream data;
    data << "{\"branch\":\"pointset-or-getpoint\",\"dsClass\":\"" << cls << "\",\"hasPointSet\":"
         << (ps ? 1 : 0) << ",\"hasPoints\":" << (pts ? 1 : 0) << ",\"npts\":" << npts << "}";
    DebugLog("CgnsWriter.cpp:GetStructuredCoords", "branch-selected", data.str(), "H1");
  }
  // #endregion

  double p[3] = { 0, 0, 0 };
  for (vtkIdType id = 0; id < npts; ++id)
  {
    if (pts)
    {
      pts->GetPoint(id, p);
    }
    else
    {
      ds->GetPoint(id, p);
    }
    c.x[id] = p[0];
    c.y[id] = (physDim >= 2) ? p[1] : 0.0;
    c.z[id] = (physDim >= 3) ? p[2] : 0.0;
  }
  return c;
}

Coords GetUnstructuredCoords(vtkDataSet* ds, const int physDim)
{
  Coords c;
  if (!ds)
  {
    return c;
  }
  const vtkIdType npts = ds->GetNumberOfPoints();
  // #region agent log
  {
    const std::string cls = ds ? EscapeJson(ds->GetClassName()) : "null";
    std::ostringstream data;
    data << "{\"dsClass\":\"" << cls << "\",\"npts\":" << npts << "}";
    DebugLog("CgnsWriter.cpp:GetUnstructuredCoords", "unstructured-coords", data.str(), "H3");
  }
  // #endregion
  c.x.resize(npts);
  c.y.resize(npts);
  c.z.resize(npts);

  double p[3] = { 0, 0, 0 };
  for (vtkIdType id = 0; id < npts; ++id)
  {
    ds->GetPoint(id, p);
    c.x[id] = p[0];
    c.y[id] = (physDim >= 2) ? p[1] : 0.0;
    c.z[id] = (physDim >= 3) ? p[2] : 0.0;
  }
  return c;
}

struct Section
{
  CGNS_ENUMT(ElementType_t) type = CGNS_ENUMV(ElementTypeNull);
  std::string name;
  int nodesPerElem = 0;

  std::vector<vtkIdType> vtkCellIds;
  std::vector<cgsize_t> conn;

  cgsize_t start = 0;
  cgsize_t end = 0;
};

bool MapVtkCellToCgns(const int vtkCellType, CGNS_ENUMT(ElementType_t)& outType, int& outNodes)
{
  // This is a minimal mapping. Extend as needed.
  switch (vtkCellType)
  {
    case VTK_VERTEX:
      outType = CGNS_ENUMV(NODE);
      outNodes = 1;
      return true;
    case VTK_LINE:
      outType = CGNS_ENUMV(BAR_2);
      outNodes = 2;
      return true;
    case VTK_TRIANGLE:
      outType = CGNS_ENUMV(TRI_3);
      outNodes = 3;
      return true;
    case VTK_QUAD:
      outType = CGNS_ENUMV(QUAD_4);
      outNodes = 4;
      return true;
    case VTK_TETRA:
      outType = CGNS_ENUMV(TETRA_4);
      outNodes = 4;
      return true;
    case VTK_PYRAMID:
      outType = CGNS_ENUMV(PYRA_5);
      outNodes = 5;
      return true;
    case VTK_WEDGE:
      outType = CGNS_ENUMV(PENTA_6);
      outNodes = 6;
      return true;
    case VTK_HEXAHEDRON:
      outType = CGNS_ENUMV(HEXA_8);
      outNodes = 8;
      return true;
    default:
      return false;
  }
}

std::string DefaultSectionName(CGNS_ENUMT(ElementType_t) t)
{
  switch (t)
  {
    case CGNS_ENUMV(NODE):
      return "Nodes";
    case CGNS_ENUMV(BAR_2):
      return "Bars";
    case CGNS_ENUMV(TRI_3):
      return "Tris";
    case CGNS_ENUMV(QUAD_4):
      return "Quads";
    case CGNS_ENUMV(TETRA_4):
      return "Tets";
    case CGNS_ENUMV(PYRA_5):
      return "Pyrs";
    case CGNS_ENUMV(PENTA_6):
      return "Wedges";
    case CGNS_ENUMV(HEXA_8):
      return "Hexes";
    default:
      return "Elements";
  }
}

vtkUnsignedCharArray* GetGhostCellArray(vtkDataSet* ds)
{
  if (!ds)
  {
    return nullptr;
  }
  auto* ghost = ds->GetCellData()->GetArray(vtkDataSetAttributes::GhostArrayName());
  return vtkUnsignedCharArray::SafeDownCast(ghost);
}

void WriteCoords(int fn, int B, int Z, const Coords& c)
{
  int Cx = 0, Cy = 0, Cz = 0;
  CheckCg(cg_coord_write(fn, B, Z, CGNS_ENUMV(RealDouble), "CoordinateX", c.x.data(), &Cx),
          "cg_coord_write(CoordinateX)");
  CheckCg(cg_coord_write(fn, B, Z, CGNS_ENUMV(RealDouble), "CoordinateY", c.y.data(), &Cy),
          "cg_coord_write(CoordinateY)");
  CheckCg(cg_coord_write(fn, B, Z, CGNS_ENUMV(RealDouble), "CoordinateZ", c.z.data(), &Cz),
          "cg_coord_write(CoordinateZ)");
}

std::string ComponentSuffix(const int c)
{
  // Common convention
  static const char* names[] = { "X", "Y", "Z", "W" };
  if (c >= 0 && c < 4)
  {
    return names[c];
  }
  return "C" + std::to_string(c);
}

void WriteFlowSolutionPointData(int fn, int B, int Z, vtkDataSet* ds)
{
  vtkPointData* pd = ds->GetPointData();
  if (!pd)
  {
    return;
  }

  int solId = 0;
  CheckCg(cg_sol_write(fn, B, Z, "PointData", CGNS_ENUMV(Vertex), &solId), "cg_sol_write(PointData)");

  const vtkIdType npts = ds->GetNumberOfPoints();

  for (int ai = 0; ai < pd->GetNumberOfArrays(); ++ai)
  {
    vtkDataArray* arr = pd->GetArray(ai);
    if (!arr)
    {
      continue;
    }
    const char* aname = arr->GetName();
    std::string baseName = aname ? aname : ("PointArray_" + std::to_string(ai));

    const int ncomp = arr->GetNumberOfComponents();
    for (int c = 0; c < ncomp; ++c)
    {
      std::string fieldName = (ncomp == 1) ? baseName : (baseName + "_" + ComponentSuffix(c));
      std::vector<double> values(static_cast<size_t>(npts));

      for (vtkIdType id = 0; id < npts; ++id)
      {
        values[static_cast<size_t>(id)] = arr->GetComponent(id, c);
      }

      int fldId = 0;
      CheckCg(cg_field_write(fn, B, Z, solId, CGNS_ENUMV(RealDouble), fieldName.c_str(), values.data(),
                             &fldId),
              "cg_field_write(point:" + fieldName + ")");
    }
  }
}

void WriteFlowSolutionCellData(int fn, int B, int Z, vtkDataSet* ds,
                               const std::vector<cgsize_t>& cellToElem,
                               const cgsize_t nCellsWritten)
{
  vtkCellData* cd = ds->GetCellData();
  if (!cd)
  {
    return;
  }

  int solId = 0;
  CheckCg(cg_sol_write(fn, B, Z, "CellData", CGNS_ENUMV(CellCenter), &solId), "cg_sol_write(CellData)");

  for (int ai = 0; ai < cd->GetNumberOfArrays(); ++ai)
  {
    vtkDataArray* arr = cd->GetArray(ai);
    if (!arr)
    {
      continue;
    }
    const char* aname = arr->GetName();
    std::string baseName = aname ? aname : ("CellArray_" + std::to_string(ai));

    const int ncomp = arr->GetNumberOfComponents();
    for (int c = 0; c < ncomp; ++c)
    {
      std::string fieldName = (ncomp == 1) ? baseName : (baseName + "_" + ComponentSuffix(c));
      std::vector<double> values(static_cast<size_t>(nCellsWritten), 0.0);

      const vtkIdType nCells = ds->GetNumberOfCells();
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        const cgsize_t elem = cellToElem[static_cast<size_t>(cid)];
        if (elem == 0)
        {
          continue; // skipped (e.g., ghost cell)
        }
        const size_t idx = static_cast<size_t>(elem - 1);
        values[idx] = arr->GetComponent(cid, c);
      }

      int fldId = 0;
      CheckCg(cg_field_write(fn, B, Z, solId, CGNS_ENUMV(RealDouble), fieldName.c_str(), values.data(),
                             &fldId),
              "cg_field_write(cell:" + fieldName + ")");
    }
  }
}

void WriteZoneStructured(int fn, int B, const std::string& zoneName, vtkDataSet* ds,
                         const CgnsWriterOptions& opt)
{
  int dims[3] = { 1, 1, 1 };
  if (auto* img = vtkImageData::SafeDownCast(ds))
  {
    img->GetDimensions(dims);
  }
  else if (auto* rg = vtkRectilinearGrid::SafeDownCast(ds))
  {
    rg->GetDimensions(dims);
  }
  else if (auto* sg = vtkStructuredGrid::SafeDownCast(ds))
  {
    sg->GetDimensions(dims);
  }
  else
  {
    throw std::runtime_error("Internal error: WriteZoneStructured called for non-structured dataset.");
  }

  const int physDim = InferPhysicalDim(ds);
  const int cellDim = (dims[2] > 1) ? 3 : ((dims[1] > 1) ? 2 : 1);

  // CGNS expects sizes for structured zones: [nVertexI,nVertexJ,nVertexK,nCellI,nCellJ,nCellK,nBndI,nBndJ,nBndK]
  cgsize_t size[9] = { 0 };
  size[0] = dims[0];
  size[1] = dims[1];
  size[2] = dims[2];
  size[3] = std::max(dims[0] - 1, 0);
  size[4] = std::max(dims[1] - 1, 0);
  size[5] = std::max(dims[2] - 1, 0);
  size[6] = 0;
  size[7] = 0;
  size[8] = 0;

  int Z = 0;
  CheckCg(cg_zone_write(fn, B, zoneName.c_str(), size, CGNS_ENUMV(Structured), &Z),
          "cg_zone_write(Structured)");

  // Coords
  Coords c = GetStructuredCoords(ds, dims, physDim);
  WriteCoords(fn, B, Z, c);

  // Solutions
  if (opt.writePointData)
  {
    WriteFlowSolutionPointData(fn, B, Z, ds);
  }

  if (opt.writeCellData)
  {
    // Structured cell ordering in VTK matches the implicit ordering for CGNS in most practical cases.
    const vtkIdType nCells = ds->GetNumberOfCells();
    std::vector<cgsize_t> cellToElem(static_cast<size_t>(nCells), 0);
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      cellToElem[static_cast<size_t>(cid)] = static_cast<cgsize_t>(cid + 1);
    }
    WriteFlowSolutionCellData(fn, B, Z, ds, cellToElem, static_cast<cgsize_t>(nCells));
  }

  (void)cellDim; // currently only used for documentation/possible future extension
  (void)physDim;
}

void WriteZoneUnstructured(int fn, int B, const std::string& zoneName, vtkDataSet* ds,
                           const CgnsWriterOptions& opt)
{
  const int physDim = InferPhysicalDim(ds);

  // Build element sections (group by CGNS element type)
  std::vector<Section> sections;
  std::unordered_map<int, size_t> typeToSectionIndex; // key: ElementType_t integer

  vtkNew<vtkIdList> ptIds;
  vtkUnsignedCharArray* ghost = opt.skipGhostCells ? GetGhostCellArray(ds) : nullptr;

  const vtkIdType nCells = ds->GetNumberOfCells();
  std::vector<cgsize_t> cellToElem(static_cast<size_t>(nCells), 0);

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (ghost && ghost->GetNumberOfTuples() == nCells)
    {
      const unsigned char g = ghost->GetValue(cid);
      if (g != 0)
      {
        continue;
      }
    }

    const int vtkType = ds->GetCellType(cid);
    CGNS_ENUMT(ElementType_t) cgnsType = CGNS_ENUMV(ElementTypeNull);
    int nodesPerElem = 0;

    if (!MapVtkCellToCgns(vtkType, cgnsType, nodesPerElem))
    {
      throw std::runtime_error("Unsupported VTK cell type " + std::to_string(vtkType) +
                               " (only a minimal subset is implemented).");
    }

    ds->GetCellPoints(cid, ptIds);
    if (ptIds->GetNumberOfIds() != nodesPerElem)
    {
      // Some VTK cell types can have variable size; we don't support that here.
      throw std::runtime_error("Unexpected number of points for VTK cell type " + std::to_string(vtkType));
    }

    const int key = static_cast<int>(cgnsType);
    size_t sidx = 0;
    auto it = typeToSectionIndex.find(key);
    if (it == typeToSectionIndex.end())
    {
      Section s;
      s.type = cgnsType;
      s.nodesPerElem = nodesPerElem;
      s.name = DefaultSectionName(cgnsType);
      sections.push_back(std::move(s));
      sidx = sections.size() - 1;
      typeToSectionIndex[key] = sidx;
    }
    else
    {
      sidx = it->second;
    }

    Section& s = sections[sidx];
    s.vtkCellIds.push_back(cid);
    for (int pi = 0; pi < nodesPerElem; ++pi)
    {
      cgsize_t id = static_cast<cgsize_t>(ptIds->GetId(pi));
      if (opt.oneBasedConnectivity)
      {
        id += 1;
      }
      s.conn.push_back(id);
    }
  }

  // Assign element ranges and build cell->element mapping
  cgsize_t elem = 1;
  for (auto& s : sections)
  {
    const cgsize_t ne = static_cast<cgsize_t>(s.vtkCellIds.size());
    if (ne == 0)
    {
      continue;
    }
    s.start = elem;
    s.end = elem + ne - 1;

    for (cgsize_t i = 0; i < ne; ++i)
    {
      const vtkIdType cid = s.vtkCellIds[static_cast<size_t>(i)];
      cellToElem[static_cast<size_t>(cid)] = s.start + i;
    }

    elem = s.end + 1;
  }

  const cgsize_t nCellsWritten = elem - 1;
  const cgsize_t nVerts = static_cast<cgsize_t>(ds->GetNumberOfPoints());

  cgsize_t size[3] = { 0 };
  size[0] = nVerts;
  size[1] = nCellsWritten;
  size[2] = 0;

  int Z = 0;
  CheckCg(cg_zone_write(fn, B, zoneName.c_str(), size, CGNS_ENUMV(Unstructured), &Z),
          "cg_zone_write(Unstructured)");

  // Coords
  Coords c = GetUnstructuredCoords(ds, physDim);
  WriteCoords(fn, B, Z, c);

  // Sections
  for (auto& s : sections)
  {
    if (s.vtkCellIds.empty())
    {
      continue;
    }
    int S = 0;
    CheckCg(cg_section_write(fn, B, Z, s.name.c_str(), s.type, s.start, s.end, 0, s.conn.data(), &S),
            "cg_section_write(" + s.name + ")");
  }

  // Solutions
  if (opt.writePointData)
  {
    WriteFlowSolutionPointData(fn, B, Z, ds);
  }

  if (opt.writeCellData)
  {
    WriteFlowSolutionCellData(fn, B, Z, ds, cellToElem, nCellsWritten);
  }
}

bool IsStructured(vtkDataSet* ds)
{
  return vtkImageData::SafeDownCast(ds) != nullptr || vtkRectilinearGrid::SafeDownCast(ds) != nullptr ||
         vtkStructuredGrid::SafeDownCast(ds) != nullptr;
}

} // end anon namespace

void CgnsWriter::Write(vtkDataObject* input, const std::string& fileName, const CgnsWriterOptions& opt)
{
  if (!input)
  {
    throw std::runtime_error("CgnsWriter::Write: input is null");
  }
  if (fileName.empty())
  {
    throw std::runtime_error("CgnsWriter::Write: fileName is empty");
  }

  // Best-effort file type selection (only affects newly created files).
#ifdef CG_FILE_HDF5
  if (opt.useHdf5)
  {
    // If CGNS was built without HDF5 support, this may fail; ignore in that case.
    (void)cg_set_file_type(CG_FILE_HDF5);
  }
  else
  {
    (void)cg_set_file_type(CG_FILE_ADF);
  }
#endif

  int fn = 0;
  CheckCg(cg_open(fileName.c_str(), CG_MODE_WRITE, &fn), "cg_open");

  try
  {
    // Zones to write
    std::vector<ZoneInput> zones = FlattenToZones(input, opt);
    if (zones.empty())
    {
      throw std::runtime_error("No vtkDataSet leaves found in input.");
    }

    // Infer dims from the first zone; CGNS base dims apply to all zones.
    vtkDataSet* first = zones[0].ds;
    const int physDim = InferPhysicalDim(first);
    const int cellDim = InferCellDim(first);

    int B = 0;
    CheckCg(cg_base_write(fn, opt.baseName.c_str(), cellDim, physDim, &B), "cg_base_write");

    for (const auto& z : zones)
    {
      vtkDataSet* ds = z.ds;
      if (!ds)
      {
        continue;
      }

      if (IsStructured(ds))
      {
        WriteZoneStructured(fn, B, z.zoneName, ds, opt);
      }
      else
      {
        WriteZoneUnstructured(fn, B, z.zoneName, ds, opt);
      }
    }

    CheckCg(cg_close(fn), "cg_close");
  }
  catch (...)
  {
    // Ensure file is closed on error
    cg_close(fn);
    throw;
  }
}
