#include "CgnsWriterCore.h"

#include <cgnslib.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
thread_local std::string g_last_error;

void SetLastError(const std::string& msg)
{
  g_last_error = msg;
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

constexpr unsigned char VTK_VERTEX = 1;
constexpr unsigned char VTK_LINE = 3;
constexpr unsigned char VTK_TRIANGLE = 5;
constexpr unsigned char VTK_QUAD = 9;
constexpr unsigned char VTK_TETRA = 10;
constexpr unsigned char VTK_HEXAHEDRON = 12;
constexpr unsigned char VTK_WEDGE = 13;
constexpr unsigned char VTK_PYRAMID = 14;

bool MapVtkCellToCgns(const unsigned char vtkCellType,
                      CGNS_ENUMT(ElementType_t)& outType,
                      int& outNodes,
                      int& outDim)
{
  switch (vtkCellType)
  {
    case VTK_VERTEX:
      outType = CGNS_ENUMV(NODE);
      outNodes = 1;
      outDim = 0;
      return true;
    case VTK_LINE:
      outType = CGNS_ENUMV(BAR_2);
      outNodes = 2;
      outDim = 1;
      return true;
    case VTK_TRIANGLE:
      outType = CGNS_ENUMV(TRI_3);
      outNodes = 3;
      outDim = 2;
      return true;
    case VTK_QUAD:
      outType = CGNS_ENUMV(QUAD_4);
      outNodes = 4;
      outDim = 2;
      return true;
    case VTK_TETRA:
      outType = CGNS_ENUMV(TETRA_4);
      outNodes = 4;
      outDim = 3;
      return true;
    case VTK_PYRAMID:
      outType = CGNS_ENUMV(PYRA_5);
      outNodes = 5;
      outDim = 3;
      return true;
    case VTK_WEDGE:
      outType = CGNS_ENUMV(PENTA_6);
      outNodes = 6;
      outDim = 3;
      return true;
    case VTK_HEXAHEDRON:
      outType = CGNS_ENUMV(HEXA_8);
      outNodes = 8;
      outDim = 3;
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

int64_t GetOffset(const UnstructuredMeshInfo& mesh, const int64_t index)
{
  if (mesh.use_64bit_ids)
  {
    const auto* offsets = static_cast<const int64_t*>(mesh.offsets);
    return offsets[index];
  }
  const auto* offsets = static_cast<const int32_t*>(mesh.offsets);
  return static_cast<int64_t>(offsets[index]);
}

int64_t GetConn(const UnstructuredMeshInfo& mesh, const int64_t index)
{
  if (mesh.use_64bit_ids)
  {
    const auto* conn = static_cast<const int64_t*>(mesh.connectivity);
    return conn[index];
  }
  const auto* conn = static_cast<const int32_t*>(mesh.connectivity);
  return static_cast<int64_t>(conn[index]);
}

struct Section
{
  CGNS_ENUMT(ElementType_t) type = CGNS_ENUMV(ElementTypeNull);
  std::string name;
  int nodesPerElem = 0;
  std::vector<cgsize_t> conn;
  cgsize_t start = 0;
  cgsize_t end = 0;
};
} // namespace

int cgns_writer::WriteUnstructured(const UnstructuredMeshInfo& mesh,
                                   const char* output_path,
                                   const CgnsWriteOptions* options)
{
  try
  {
    if (!output_path || output_path[0] == '\0')
    {
      throw std::runtime_error("output_path is null or empty");
    }
    if (!mesh.points || mesh.num_points <= 0)
    {
      throw std::runtime_error("mesh.points is null or num_points <= 0");
    }
    if (!mesh.connectivity || mesh.connectivity_size <= 0)
    {
      throw std::runtime_error("mesh.connectivity is null or connectivity_size <= 0");
    }
    if (!mesh.offsets || mesh.num_cells <= 0)
    {
      throw std::runtime_error("mesh.offsets is null or num_cells <= 0");
    }
    if (!mesh.types)
    {
      throw std::runtime_error("mesh.types is null");
    }

    const bool useHdf5 = !options || options->use_hdf5 != 0;
    const char* baseName =
      (options && options->base_name && options->base_name[0] != '\0') ? options->base_name : "Base";
    const char* zoneName =
      (options && options->zone_name && options->zone_name[0] != '\0') ? options->zone_name : "Zone0";

#ifdef CG_FILE_HDF5
    if (useHdf5)
    {
      (void)cg_set_file_type(CG_FILE_HDF5);
    }
    else
    {
      (void)cg_set_file_type(CG_FILE_ADF);
    }
#else
    (void)useHdf5;
#endif

    int fn = 0;
    CheckCg(cg_open(output_path, CG_MODE_WRITE, &fn), "cg_open");

    try
    {
      std::vector<double> x(static_cast<size_t>(mesh.num_points));
      std::vector<double> y(static_cast<size_t>(mesh.num_points));
      std::vector<double> z(static_cast<size_t>(mesh.num_points));
      for (int64_t i = 0; i < mesh.num_points; ++i)
      {
        const int64_t base = i * 3;
        x[static_cast<size_t>(i)] = mesh.points[base + 0];
        y[static_cast<size_t>(i)] = mesh.points[base + 1];
        z[static_cast<size_t>(i)] = mesh.points[base + 2];
      }

      std::vector<Section> sections;
      std::unordered_map<int, size_t> typeToSectionIndex;

      int cellDim = 0;

      for (int64_t cellId = 0; cellId < mesh.num_cells; ++cellId)
      {
        const int64_t start = GetOffset(mesh, cellId);
        const int64_t end = GetOffset(mesh, cellId + 1);
        if (start < 0 || end < start || end > mesh.connectivity_size)
        {
          throw std::runtime_error("Invalid offsets/connectivity_size for cell " + std::to_string(cellId));
        }

        const unsigned char vtkType = mesh.types[cellId];
        CGNS_ENUMT(ElementType_t) cgnsType = CGNS_ENUMV(ElementTypeNull);
        int nodesPerElem = 0;
        int elemDim = 0;
        if (!MapVtkCellToCgns(vtkType, cgnsType, nodesPerElem, elemDim))
        {
          throw std::runtime_error("Unsupported VTK cell type " + std::to_string(vtkType));
        }

        const int64_t cellSize = end - start;
        if (cellSize != nodesPerElem)
        {
          throw std::runtime_error("Cell " + std::to_string(cellId) + " has " +
                                   std::to_string(cellSize) + " nodes, expected " +
                                   std::to_string(nodesPerElem));
        }

        cellDim = std::max(cellDim, elemDim);

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
        for (int64_t i = start; i < end; ++i)
        {
          const int64_t id = GetConn(mesh, i);
          if (id < 0 || id >= mesh.num_points)
          {
            throw std::runtime_error("Connectivity id out of range at index " + std::to_string(i));
          }
          s.conn.push_back(static_cast<cgsize_t>(id + 1));
        }
      }

      cgsize_t elem = 1;
      for (auto& s : sections)
      {
        const cgsize_t ne = static_cast<cgsize_t>(s.conn.size() / static_cast<size_t>(s.nodesPerElem));
        if (ne == 0)
        {
          continue;
        }
        s.start = elem;
        s.end = elem + ne - 1;
        elem = s.end + 1;
      }

      const cgsize_t nCellsWritten = elem - 1;
      const cgsize_t nVerts = static_cast<cgsize_t>(mesh.num_points);
      const int physDim = 3;
      if (cellDim <= 0)
      {
        cellDim = 3;
      }

      int B = 0;
      CheckCg(cg_base_write(fn, baseName, cellDim, physDim, &B), "cg_base_write");

      cgsize_t size[3] = { 0 };
      size[0] = nVerts;
      size[1] = nCellsWritten;
      size[2] = 0;

      int Z = 0;
      CheckCg(cg_zone_write(fn, B, zoneName, size, CGNS_ENUMV(Unstructured), &Z),
              "cg_zone_write(Unstructured)");

      int Cx = 0, Cy = 0, Cz = 0;
      CheckCg(cg_coord_write(fn, B, Z, CGNS_ENUMV(RealDouble), "CoordinateX", x.data(), &Cx),
              "cg_coord_write(CoordinateX)");
      CheckCg(cg_coord_write(fn, B, Z, CGNS_ENUMV(RealDouble), "CoordinateY", y.data(), &Cy),
              "cg_coord_write(CoordinateY)");
      CheckCg(cg_coord_write(fn, B, Z, CGNS_ENUMV(RealDouble), "CoordinateZ", z.data(), &Cz),
              "cg_coord_write(CoordinateZ)");

      for (auto& s : sections)
      {
        if (s.conn.empty())
        {
          continue;
        }
        int S = 0;
        CheckCg(cg_section_write(fn, B, Z, s.name.c_str(), s.type, s.start, s.end, 0, s.conn.data(), &S),
                "cg_section_write(" + s.name + ")");
      }

      CheckCg(cg_close(fn), "cg_close");
    }
    catch (...)
    {
      cg_close(fn);
      throw;
    }

    SetLastError("");
    return 0;
  }
  catch (const std::exception& ex)
  {
    SetLastError(ex.what());
    return 1;
  }
}

extern "C" CGNS_WRITER_API int cgns_write_unstructured(const UnstructuredMeshInfo* mesh,
                                                       const char* output_path,
                                                       const CgnsWriteOptions* options)
{
  if (!mesh)
  {
    SetLastError("mesh is null");
    return 1;
  }
  return cgns_writer::WriteUnstructured(*mesh, output_path, options);
}

extern "C" CGNS_WRITER_API const char* cgns_get_last_error(void)
{
  return g_last_error.c_str();
}

extern "C" CGNS_WRITER_API const char* cgns_writer_version(void)
{
  return "0.1.0";
}
