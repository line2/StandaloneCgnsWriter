# standalone-cgns-writer vcpkg Port

这是 `standalone-cgns-writer` 的 vcpkg port 配置。

## 快速开始

### 使用 Overlay Ports（本地开发）

1. 确保 `vcpkg-configuration.json` 包含：
```json
{
  "overlay-ports": ["vcpkg/ports"]
}
```

2. 修改 `portfile.cmake` 使用本地源码：
```cmake
vcpkg_from_path(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
```

3. 安装：
```bash
vcpkg install standalone-cgns-writer --overlay-ports=./vcpkg/ports
```

### 使用 Git Registry（生产环境）

1. 更新 `portfile.cmake` 中的 Git URL 和版本标签
2. 配置 registry（见 `VCPKG_DEPLOYMENT.md`）
3. 安装：
```bash
vcpkg install standalone-cgns-writer
```

## 特性

- `dll`: 构建独立的 CGNS writer DLL（默认启用）

## 依赖

- `cgns`: CGNS 库
- `vtk`: VTK 库

## 安装的目标

- `StandaloneCgnsWriter::cgns_writer`: 静态库目标
- `StandaloneCgnsWriter::cgns_writer_dll`: DLL 目标（如果启用了 dll 特性）
