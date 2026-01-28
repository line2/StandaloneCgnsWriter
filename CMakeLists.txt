cmake_minimum_required(VERSION 3.20)

project(StandaloneCgnsWriter
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set runtime library to match vcpkg's x64-windows-static triplet
# This ensures all targets use static runtime library (MT/MTd) to match vcpkg's static triplet
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32)
  # For Clang with MSVC ABI, set flags to use static runtime library
  # Remove -D_DLL and ensure -D_MT is set (static runtime)
  string(REPLACE "-D_DLL" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-D_MT" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  add_compile_options(
    $<$<CONFIG:Release>:-D_MT>
    $<$<CONFIG:RelWithDebInfo>:-D_MT>
    $<$<CONFIG:MinSizeRel>:-D_MT>
    $<$<CONFIG:Debug>:-D_MTd>
  )
  # Also set the MSVC runtime library property for CMake 3.15+
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(GNUInstallDirs)

# ---- Dependencies ----
# VTK (no ParaView required)
find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
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

# Ensure static runtime library for cgns_writer
if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32))
  set_target_properties(cgns_writer PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()

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
  
  # Ensure static runtime library for cgns_writer_dll
  if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32))
    set_target_properties(cgns_writer_dll PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
  endif()

endif()

# ---- Core Example (no VTK) ----
if(BUILD_CGNS_DLL)
  add_executable(core_example
    example/core_example.cpp
  )

  target_include_directories(core_example PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>"
  )

  target_link_libraries(core_example PRIVATE
    cgns_writer_dll
    $<IF:$<TARGET_EXISTS:CGNS::cgns_shared>,CGNS::cgns_shared,CGNS::cgns_static>
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::IOLegacy
    VTK::IOXML
  )

  # Ensure static runtime library for core_example
  if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32))
    set_target_properties(core_example PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
  endif()
endif()

# ---- Installation & packaging ----
install(TARGETS cgns_writer
  EXPORT StandaloneCgnsWriterTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_CGNS_DLL)
  install(TARGETS cgns_writer_dll
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

if(BUILD_CGNS_DLL)
  install(TARGETS core_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

install(DIRECTORY src/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h"
)

install(EXPORT StandaloneCgnsWriterTargets
  NAMESPACE StandaloneCgnsWriter::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

export(EXPORT StandaloneCgnsWriterTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/StandaloneCgnsWriterTargets.cmake"
  NAMESPACE StandaloneCgnsWriter::
)

# if(WIN32)
#   set(_standalone_bundle_dirs "")

#   function(_collect_dependency_dir target)
#     if(NOT TARGET ${target})
#       return()
#     endif()

#     foreach(_prop IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG
#                   IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_MINSIZEREL)
#       get_target_property(_location ${target} ${_prop})
#       if(_location)
#         get_filename_component(_dir ${_location} DIRECTORY)
#         list(APPEND _standalone_bundle_dirs ${_dir})
#       endif()
#     endforeach()
#   endfunction()

#   _collect_dependency_dir(VTK::CommonCore)
#   _collect_dependency_dir(VTK::CommonDataModel)
#   _collect_dependency_dir(VTK::CommonExecutionModel)
#   _collect_dependency_dir(VTK::IOLegacy)
#   _collect_dependency_dir(VTK::IOXML)
#   _collect_dependency_dir(CGNS::cgns_shared)
#   _collect_dependency_dir(CGNS::cgns_static)

#   list(REMOVE_DUPLICATES _standalone_bundle_dirs)

#   if(_standalone_bundle_dirs)
#     set(_bundle_dirs_script "")
#     foreach(_bundle_dir IN LISTS _standalone_bundle_dirs)
#       file(TO_CMAKE_PATH "${_bundle_dir}" _cmake_bundle_dir)
#       set(_bundle_dirs_script "${_bundle_dirs_script} \"${_cmake_bundle_dir}\"")
#     endforeach()

#     file(TO_CMAKE_PATH "${CMAKE_INSTALL_FULL_BINDIR}" _install_bindir)

#     install(CODE "include(BundleUtilities)
# fixup_bundle(\"${_install_bindir}/vtk_to_cgns${CMAKE_EXECUTABLE_SUFFIX}\" \"${_install_bindir}\"${_bundle_dirs_script})")
#   endif()
# endif()

# set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
# set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# set(CPACK_GENERATOR "ZIP;NSIS")
# set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME}")
# file(TO_CMAKE_PATH "${CMAKE_INSTALL_BINDIR}/vtk_to_cgns${CMAKE_EXECUTABLE_SUFFIX}" _nsis_icon_path)
# set(CPACK_NSIS_INSTALLED_ICON_NAME "${_nsis_icon_path}")

# include(CPack)
