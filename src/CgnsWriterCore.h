#pragma once

#include "CgnsWriterExport.h"

namespace cgns_writer
{
// C++ 内部入口：从 CSR-like UnstructuredMeshInfo 写入 CGNS 文件。
// 返回 0 表示成功，非 0 表示失败。
CGNS_WRITER_API int WriteUnstructured(const UnstructuredMeshInfo& mesh,
                                      const char* output_path,
                                      const CgnsWriteOptions* options);
} // namespace cgns_writer
