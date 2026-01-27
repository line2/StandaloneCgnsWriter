cmake_minimum_required(VERSION 3.20)

project(StandaloneCgnsWriter
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Dependencies ----
# VTK (no ParaView required)
find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  CommonExecutionModel
  IOLegacy
  IOXML
)
find_package(cgns CONFIG REQUIRED)

# CGNS: prefer a config package if available, otherwise use our FindCGNS.cmake
# list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
#find_package(cgns QUIET CONFIG)
#if(NOT CGNS_FOUND)
#  find_package(CGNS REQUIRED MODULE)
#endif()

# ---- Library ----
add_library(cgns_writer
  src/CgnsWriter.cpp
  src/CgnsWriter.h
)

target_include_directories(cgns_writer PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>"
)

target_link_libraries(cgns_writer PUBLIC
  # CGNS::cgns
  PRIVATE $<IF:$<TARGET_EXISTS:CGNS::cgns_shared>,CGNS::cgns_shared,CGNS::cgns_static>
  VTK::CommonCore
  VTK::CommonDataModel
)

# ---- Standalone DLL (no VTK) ----
option(BUILD_CGNS_DLL "Build the standalone CGNS writer DLL" ON)

if(BUILD_CGNS_DLL)
  add_library(cgns_writer_dll SHARED
    src/CgnsWriterCore.cpp
    src/CgnsWriterCore.h
    src/CgnsWriterExport.h
  )

  target_include_directories(cgns_writer_dll PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>"
  )

  target_compile_definitions(cgns_writer_dll PRIVATE CGNS_WRITER_EXPORTS)

  target_link_libraries(cgns_writer_dll PRIVATE
    $<IF:$<TARGET_EXISTS:CGNS::cgns_shared>,CGNS::cgns_shared,CGNS::cgns_static>
  )

  set_target_properties(cgns_writer_dll PROPERTIES
    OUTPUT_NAME "cgns_writer"
    ARCHIVE_OUTPUT_NAME "cgns_writer_dll"
    IMPORT_LIBRARY_OUTPUT_NAME "cgns_writer_dll"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
  )

  install(TARGETS cgns_writer_dll
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  install(FILES src/CgnsWriterExport.h DESTINATION include)
endif()

# ---- CLI tool ----
add_executable(vtk_to_cgns
  src/main.cpp
)

target_link_libraries(vtk_to_cgns PRIVATE
  cgns_writer
  VTK::CommonExecutionModel
  VTK::IOLegacy
  VTK::IOXML
)

# For static VTK builds: ensure modules are initialized.
vtk_module_autoinit(
  TARGETS vtk_to_cgns
  MODULES ${VTK_LIBRARIES}
)
