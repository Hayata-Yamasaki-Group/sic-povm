# SPDX-License-Identifier: AGPL-3.0-or-later
# ---------------------------------------------------------------- library
add_library(sic_core
  src/core/numeric/weyl_heisenberg.cpp
  src/core/numeric/verify.cpp
  src/core/io/fiducial_io.cpp
  src/core/io/interchange.cpp
  src/core/io/csv.cpp
  src/core/io/npy.cpp
  src/core/io/npz.cpp
  src/core/io/mat.cpp
  src/core/io/hdf5.cpp
  src/core/numeric/fft.cpp
  src/core/numeric/sic_frame.cpp
  src/core/clifford/clifford.cpp
)
add_library(sic::core ALIAS sic_core)
target_compile_features(sic_core PUBLIC cxx_std_20)
target_link_libraries(sic_core PUBLIC sic_support Threads::Threads)
target_include_directories(sic_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_options(sic_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)

# SPDX-License-Identifier: AGPL-3.0-or-later
# Shared optional HDF5 capability for core interchange clients.
# HDF5 output. SIC_ENABLE_HDF5 = AUTO (default: use libhdf5 when found), ON (fail when not found) or OFF.
# The chosen state is printed, so the set of output formats never changes silently.
set(SIC_ENABLE_HDF5 "AUTO" CACHE STRING "Write fiducial.h5 in sic_core: AUTO, ON or OFF")
set_property(CACHE SIC_ENABLE_HDF5 PROPERTY STRINGS AUTO ON OFF)
if(NOT SIC_ENABLE_HDF5 STREQUAL "OFF")
  find_path(SIC_HDF5_INCLUDE_DIR hdf5.h PATH_SUFFIXES hdf5/serial)
  find_library(SIC_HDF5_LIBRARY NAMES hdf5 hdf5_serial PATH_SUFFIXES hdf5/serial)
endif()
if(SIC_HDF5_INCLUDE_DIR AND SIC_HDF5_LIBRARY AND NOT SIC_ENABLE_HDF5 STREQUAL "OFF")
  message(STATUS "libhdf5 found (SIC_ENABLE_HDF5=${SIC_ENABLE_HDF5}): sic_core writes fiducial.h5")
  target_compile_definitions(sic_core PUBLIC SIC_HAVE_HDF5=1)
  list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../dependencies")
  find_package(SicHDF5 REQUIRED)
  sic_map_dependency(SicHDF5::SicHDF5 "${SicHDF5_INCLUDE_DIR}")
  set(SIC_WITH_HDF5 ON)
  target_link_libraries(sic_core PUBLIC SicHDF5::SicHDF5)
elseif(SIC_ENABLE_HDF5 STREQUAL "ON")
  message(FATAL_ERROR "SIC_ENABLE_HDF5=ON but libhdf5 (hdf5.h and the library) was not found")
else()
  message(STATUS "HDF5 output disabled (SIC_ENABLE_HDF5=${SIC_ENABLE_HDF5}): sic_core builds without fiducial.h5")
endif()



target_sources(sic_core PRIVATE src/core/numeric/overlap_deviation.cpp src/core/numeric/frame_diagnostics.cpp)

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_CORE_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/core/clifford.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/fft.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/fiducial_io.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/hdf5.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/interchange.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/sic_frame.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/types.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/verify.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/core/weyl_heisenberg.hpp"
  "${PROJECT_SOURCE_DIR}/src/core/clifford/clifford.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/csv.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/fiducial_io.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/hdf5.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/interchange.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/mat.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/mat.hpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/npy.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/npz.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/io/npz.hpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/fft.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/frame_diagnostics.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/frame_diagnostics.hpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/overlap_deviation.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/overlap_deviation.hpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/sic_frame.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/verify.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/verify.hpp"
  "${PROJECT_SOURCE_DIR}/src/core/numeric/weyl_heisenberg.cpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)

function(sic_add_executable name source)
  add_executable(${name} ${source})
  if(SIC_BUILD_TESTS AND name MATCHES "^sic-test-")
    target_link_libraries(${name} PRIVATE sic_test_assertions)
  endif()
  target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(${name} PRIVATE sic_core)
  target_compile_options(${name} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)
endfunction()

# ---------------------------------------------------------------- optional production benchmarks
if(SIC_BUILD_BENCH)
  sic_add_executable(bench-overlaps bench/core/overlaps.cpp)
  sic_add_executable(bench-verify bench/core/verify.cpp)
endif()
