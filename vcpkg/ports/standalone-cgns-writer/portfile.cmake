# Portfile for standalone-cgns-writer
# This port can be used in two ways:
# 1. From local source (for development): Use vcpkg_from_path
# 2. From Git repository (for distribution): Use vcpkg_from_git

# Option 1: Use local source path (uncomment for local development)
# vcpkg_from_path(
#     SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.."
#     PATCHES
# )

# Option 2: Use Git repository (recommended for distribution)
# Replace with your actual Git repository URL
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/your-org/StandaloneCgnsWriter.git
    REF v0.1.0
    # For private repos, you may need to use authentication
    # See vcpkg documentation for Git authentication options
)

# Determine if DLL feature is enabled
if("dll" IN_LIST FEATURES)
    set(BUILD_DLL_OPTION ON)
else()
    set(BUILD_DLL_OPTION OFF)
endif()

# Configure CMake
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_CGNS_DLL=${BUILD_DLL_OPTION}
)

# Build and install
vcpkg_cmake_install()

# The DLL feature is handled during CMake configuration
# No additional installation steps needed

# Remove debug include directories (if any)
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

# Install copyright/license file if it exists
if(EXISTS "${SOURCE_PATH}/LICENSE")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
elseif(EXISTS "${SOURCE_PATH}/LICENSE.txt")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
endif()

# Fix CMake config files for vcpkg
vcpkg_cmake_config_fixup(
    CONFIG_PATH "lib/cmake/StandaloneCgnsWriter"
    PACKAGE_NAME "StandaloneCgnsWriter"
)

# Copy pdb files for debug builds (Windows)
if(VCPKG_TARGET_IS_WINDOWS AND VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_copy_pdbs()
endif()
