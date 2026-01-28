cmake_minimum_required(VERSION 3.20)

project(StandaloneCgnsWriter
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

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

install(TARGETS vtk_to_cgns
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

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

if(WIN32)
  set(_standalone_bundle_dirs "")

  function(_collect_dependency_dir target)
    if(NOT TARGET ${target})
      return()
    endif()

    foreach(_prop IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG
                  IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_MINSIZEREL)
      get_target_property(_location ${target} ${_prop})
      if(_location)
        get_filename_component(_dir ${_location} DIRECTORY)
        list(APPEND _standalone_bundle_dirs ${_dir})
      endif()
    endforeach()
  endfunction()

  _collect_dependency_dir(VTK::CommonCore)
  _collect_dependency_dir(VTK::CommonDataModel)
  _collect_dependency_dir(VTK::CommonExecutionModel)
  _collect_dependency_dir(VTK::IOLegacy)
  _collect_dependency_dir(VTK::IOXML)
  _collect_dependency_dir(CGNS::cgns_shared)
  _collect_dependency_dir(CGNS::cgns_static)

  list(REMOVE_DUPLICATES _standalone_bundle_dirs)

  if(_standalone_bundle_dirs)
    set(_bundle_dirs_script "")
    foreach(_bundle_dir IN LISTS _standalone_bundle_dirs)
      file(TO_CMAKE_PATH "${_bundle_dir}" _cmake_bundle_dir)
      set(_bundle_dirs_script "${_bundle_dirs_script} \"${_cmake_bundle_dir}\"")
    endforeach()

    file(TO_CMAKE_PATH "${CMAKE_INSTALL_FULL_BINDIR}" _install_bindir)

    install(CODE "include(BundleUtilities)
fixup_bundle(\"${_install_bindir}/vtk_to_cgns${CMAKE_EXECUTABLE_SUFFIX}\" \"${_install_bindir}\"${_bundle_dirs_script})")
  endif()
endif()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP;NSIS")
set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME}")
file(TO_CMAKE_PATH "${CMAKE_INSTALL_BINDIR}/vtk_to_cgns${CMAKE_EXECUTABLE_SUFFIX}" _nsis_icon_path)
set(CPACK_NSIS_INSTALLED_ICON_NAME "${_nsis_icon_path}")

include(CPack)
