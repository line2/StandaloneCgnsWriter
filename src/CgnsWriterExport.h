#pragma once

#include <stdint.h>

#ifdef _WIN32
#  ifdef CGNS_WRITER_EXPORTS
#    define CGNS_WRITER_API __declspec(dllexport)
#  else
#    define CGNS_WRITER_API __declspec(dllimport)
#  endif
#else
#  define CGNS_WRITER_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // --- 节点数据 ---
    double* points;           // [x0, y0, z0, x1, y1, z1, ...]
    int64_t num_points;       // 顶点数量

    // --- 拓扑数据 ---
    void* connectivity;       // CSR 连接数组
    int64_t connectivity_size;// 连接数组长度

    void* offsets;            // CSR 偏移量数组 (长度 = num_cells + 1)
    int64_t num_cells;        // 单元数量

    unsigned char* types;     // VTK 单元类型数组 (VTK_WEDGE=13, etc.)

    // --- 格式标志 ---
    int use_64bit_ids;        // connectivity/offsets 是 1 = int64_t*, 0 = int32_t*
} UnstructuredMeshInfo;

typedef struct {
    int use_hdf5;            // 1=HDF5(默认), 0=ADF
    const char* base_name;   // CGNS base 名称，NULL="Base"
    const char* zone_name;   // Zone 名称，NULL="Zone0"
} CgnsWriteOptions;

// 返回 0 表示成功，非 0 表示失败。失败原因可通过 cgns_get_last_error 获取。
CGNS_WRITER_API int cgns_write_unstructured(const UnstructuredMeshInfo* mesh,
                                            const char* output_path,
                                            const CgnsWriteOptions* options);

// 返回最近一次失败的错误信息（线程局部存储）。
CGNS_WRITER_API const char* cgns_get_last_error(void);

// 返回库版本字符串。
CGNS_WRITER_API const char* cgns_writer_version(void);

#ifdef __cplusplus
} // extern "C"
#endif
