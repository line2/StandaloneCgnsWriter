# vcpkg 私有部署快速开始

## 场景 1: 本地开发测试（最简单）

### 步骤

1. **修改 portfile.cmake** 使用本地源码：

编辑 `vcpkg/ports/standalone-cgns-writer/portfile.cmake`，注释掉 Git 部分，启用本地路径：

```cmake
# 注释掉这部分：
# vcpkg_from_git(...)

# 启用这部分：
vcpkg_from_path(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
```

2. **安装包**：

```powershell
vcpkg install standalone-cgns-writer --overlay-ports=./vcpkg/ports
```

3. **在其他项目中使用**：

在项目的 `vcpkg.json` 中添加：
```json
{
  "dependencies": ["standalone-cgns-writer"]
}
```

## 场景 2: 团队私有部署（Git Registry）

### 步骤

1. **创建私有 Git 仓库**（用于存放 registry）：
   ```
   your-vcpkg-registry/
   ├── ports/
   │   └── standalone-cgns-writer/
   │       ├── vcpkg.json
   │       └── portfile.cmake
   └── versions/
       └── s/
           └── standalone-cgns-writer.json
   ```

2. **更新 portfile.cmake** 中的 Git URL：

```cmake
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://your-git-server.com/your-org/StandaloneCgnsWriter.git
    REF v0.1.0  # 或使用 commit hash
)
```

3. **配置认证**（私有仓库）：

```powershell
# SSH 方式（推荐）
git config --global url."git@your-git-server.com:".insteadOf "https://your-git-server.com/"

# 或使用 Token
git config --global url."https://your-token@your-git-server.com/".insteadOf "https://your-git-server.com/"
```

4. **在项目中使用 registry**：

在项目的 `vcpkg-configuration.json` 中添加：
```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://your-git-server.com/your-org/vcpkg-registry.git",
      "baseline": "main",
      "packages": ["standalone-cgns-writer"]
    }
  ]
}
```

5. **生成版本哈希**：

```powershell
cd your-vcpkg-registry
vcpkg x-add-version standalone-cgns-writer --overlay-ports=./ports
```

6. **提交并推送 registry**：

```powershell
git add .
git commit -m "Add standalone-cgns-writer v0.1.0"
git push
```

## 场景 3: 完全离线部署（本地 Registry）

### 步骤

1. **创建本地 registry 目录结构**：
   ```
   C:\vcpkg-registry\
   ├── ports\
   │   └── standalone-cgns-writer\
   │       ├── vcpkg.json
   │       └── portfile.cmake
   └── versions\
       └── s\
           └── standalone-cgns-writer.json
   ```

2. **修改 portfile.cmake** 使用本地路径或压缩包：

```cmake
# 选项 A: 直接使用本地路径
vcpkg_from_path(
    SOURCE_PATH "C:/path/to/StandaloneCgnsWriter"
)

# 选项 B: 使用压缩包
vcpkg_download_distfile(ARCHIVE
    URLS "file:///C:/path/to/StandaloneCgnsWriter-0.1.0.zip"
    FILENAME "StandaloneCgnsWriter-0.1.0.zip"
    SHA512 <计算得到的哈希值>
)
vcpkg_extract_source_archive(SOURCE_PATH)
```

3. **配置本地 registry**：

在项目的 `vcpkg-configuration.json` 中：
```json
{
  "registries": [
    {
      "kind": "filesystem",
      "path": "C:/vcpkg-registry",
      "packages": ["standalone-cgns-writer"]
    }
  ]
}
```

## 验证安装

安装后，验证包是否正确安装：

```powershell
vcpkg list standalone-cgns-writer
```

应该看到类似输出：
```
standalone-cgns-writer:x64-windows:0.1.0#0
```

## 在 CMake 项目中使用

```cmake
find_package(StandaloneCgnsWriter CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE StandaloneCgnsWriter::cgns_writer)
```

## 常见问题

**Q: 如何更新版本？**
A: 修改 `vcpkg.json` 中的版本号，然后运行 `vcpkg x-add-version` 更新版本文件。

**Q: 私有 Git 仓库认证失败？**
A: 检查 Git 配置，确保 SSH key 或 Token 正确配置。

**Q: 如何测试本地更改？**
A: 使用 overlay-ports 方式，修改 port 文件后重新安装即可。

## 更多信息

详细文档请参考 `VCPKG_DEPLOYMENT.md`。
