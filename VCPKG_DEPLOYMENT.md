# vcpkg 私有部署指南

本文档说明如何通过 vcpkg 发布和私有部署 `standalone-cgns-writer` 库。

## 目录

1. [部署方式概览](#部署方式概览)
2. [方式一：使用 Overlay Ports（推荐用于本地开发）](#方式一使用-overlay-ports推荐用于本地开发)
3. [方式二：使用 Git Registry（推荐用于团队部署）](#方式二使用-git-registry推荐用于团队部署)
4. [方式三：使用本地 Registry](#方式三使用本地-registry)
5. [使用私有包](#使用私有包)
6. [CI/CD 集成](#cicd-集成)

## 部署方式概览

vcpkg 支持三种主要的私有部署方式：

- **Overlay Ports**: 最简单，适合本地开发和测试
- **Git Registry**: 适合团队协作，通过 Git 仓库管理版本
- **本地 Registry**: 适合企业内网环境，完全离线部署

## 方式一：使用 Overlay Ports（推荐用于本地开发）

Overlay Ports 允许你在不修改 vcpkg 仓库的情况下添加自定义端口。

### 步骤 1: 准备 Port 文件

确保 `vcpkg/ports/standalone-cgns-writer/` 目录包含：
- `vcpkg.json`
- `portfile.cmake`

### 步骤 2: 配置 vcpkg-configuration.json

在你的项目根目录的 `vcpkg-configuration.json` 中（已存在），确保包含 overlay-ports：

```json
{
  "overlay-ports": [
    "vcpkg/ports"
  ]
}
```

### 步骤3: 使用本地源码（开发模式）

如果你在开发过程中想直接使用本地源码，修改 `portfile.cmake`：

```cmake
# 在 portfile.cmake 开头，注释掉 vcpkg_from_git，使用：
vcpkg_from_path(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
```

### 步骤 4: 安装包

```bash
vcpkg install standalone-cgns-writer --overlay-ports=./vcpkg/ports
```

或者在项目的 `vcpkg.json` 中直接添加依赖：

```json
{
  "dependencies": [
    "standalone-cgns-writer"
  ]
}
```

## 方式二：使用 Git Registry（推荐用于团队部署）

Git Registry 允许你通过 Git 仓库分发私有包，支持版本管理和团队协作。

### 步骤 1: 创建私有 Git 仓库

创建一个新的 Git 仓库来存放你的 vcpkg registry：

```
your-private-registry/
├── ports/
│   └── standalone-cgns-writer/
│       ├── vcpkg.json
│       └── portfile.cmake
└── versions/
    └── s/
        └── standalone-cgns-writer.json
```

### 步骤 2: 创建版本文件

创建 `versions/s/standalone-cgns-writer.json`：

```json
{
  "versions": [
    {
      "version": "0.1.0",
      "port-version": 0,
      "git-tree": "abc123def456..."  // 使用 vcpkg x-add-version 生成
    }
  ]
}
```

### 步骤 3: 更新 portfile.cmake

确保 `portfile.cmake` 使用 `vcpkg_from_git` 从你的私有仓库获取源码：

```cmake
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://your-git-server.com/your-org/StandaloneCgnsWriter.git
    REF v0.1.0
)
```

### 步骤 4: 配置 Registry

在项目的 `vcpkg-configuration.json` 中添加 registry：

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://your-git-server.com/your-org/vcpkg-registry.git",
      "baseline": "main",
      "packages": ["standalone-cgns-writer"]
    }
  ],
  "overlay-ports": [
    "vcpkg/ports"
  ]
}
```

### 步骤 5: 认证配置（私有仓库）

对于私有 Git 仓库，需要配置认证：

**使用 SSH（推荐）**:
```bash
# 确保 SSH key 已配置
git config --global url."git@your-git-server.com:".insteadOf "https://your-git-server.com/"
```

**使用 Personal Access Token**:
```bash
# 在 URL 中包含 token
git config --global url."https://token@your-git-server.com/".insteadOf "https://your-git-server.com/"
```

### 步骤 6: 生成版本哈希

在 registry 仓库中运行：

```bash
cd your-private-registry
vcpkg x-add-version standalone-cgns-writer --overlay-ports=./ports
```

这会自动更新 `versions/s/standalone-cgns-writer.json` 中的 `git-tree` 哈希。

## 方式三：使用本地 Registry

适合完全离线或内网环境。

### 步骤 1: 创建本地 Registry 结构

```
local-registry/
├── ports/
│   └── standalone-cgns-writer/
│       ├── vcpkg.json
│       └── portfile.cmake
└── versions/
    └── s/
        └── standalone-cgns-writer.json
```

### 步骤 2: 配置本地 Registry

在 `vcpkg-configuration.json` 中：

```json
{
  "registries": [
    {
      "kind": "filesystem",
      "path": "C:/path/to/local-registry",
      "packages": ["standalone-cgns-writer"]
    }
  ]
}
```

### 步骤 3: 使用本地源码或压缩包

在 `portfile.cmake` 中，可以使用：

```cmake
# 选项 A: 从本地路径
vcpkg_from_path(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.."
)

# 选项 B: 从压缩包
vcpkg_download_distfile(ARCHIVE
    URLS "file:///C:/path/to/StandaloneCgnsWriter-0.1.0.zip"
    FILENAME "StandaloneCgnsWriter-0.1.0.zip"
    SHA512 <hash>
)
vcpkg_extract_source_archive(SOURCE_PATH)
```

## 使用私有包

### 在项目中使用

在你的项目的 `vcpkg.json` 中：

```json
{
  "name": "my-project",
  "version": "1.0.0",
  "dependencies": [
    "standalone-cgns-writer",
    {
      "name": "standalone-cgns-writer",
      "features": ["dll"]
    }
  ]
}
```

### 安装特定版本

```bash
vcpkg install standalone-cgns-writer@0.1.0
```

### 安装特定特性

```bash
# 安装带 DLL 特性
vcpkg install standalone-cgns-writer[core,dll]

# 安装不带 DLL 特性
vcpkg install standalone-cgns-writer[core]
```

## CI/CD 集成

### GitHub Actions 示例

```yaml
name: Build with vcpkg

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup vcpkg
        uses: microsoft/setup-vcpkg@v1
        with:
          vcpkgGitCommitSha: <commit-sha>
      
      - name: Configure Git for private registry
        run: |
          git config --global url."https://${{ secrets.GIT_TOKEN }}@github.com/".insteadOf "https://github.com/"
      
      - name: Configure CMake
        run: |
          cmake --preset default
      
      - name: Build
        run: |
          cmake --build --preset default
```

### Azure DevOps 示例

```yaml
steps:
  - task: CMake@1
    inputs:
      cmakeArgs: |
        -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
        -DVCPKG_FEATURE_FLAGS=manifests
```

## 常见问题

### Q: 如何更新包版本？

A: 更新 `vcpkg.json` 中的版本号，然后运行 `vcpkg x-add-version` 更新版本文件。

### Q: 私有 Git 仓库认证失败？

A: 确保配置了正确的 SSH key 或 Personal Access Token，并检查 Git URL 格式。

### Q: 如何测试本地 port 更改？

A: 使用 overlay-ports 方式，直接修改 `vcpkg/ports/standalone-cgns-writer/` 中的文件，然后重新安装。

### Q: 支持哪些 triplet？

A: 包支持所有 vcpkg 标准 triplet（如 `x64-windows`, `x64-windows-static`, `x64-linux` 等）。

## 参考资源

- [vcpkg 文档 - Overlay Ports](https://github.com/Microsoft/vcpkg/blob/master/docs/specifications/ports-overlay.md)
- [vcpkg 文档 - Registries](https://github.com/Microsoft/vcpkg/blob/master/docs/specifications/registries.md)
- [vcpkg 文档 - Versioning](https://github.com/Microsoft/vcpkg/blob/master/docs/specifications/versioning.md)
