# SPDX-License-Identifier: AGPL-3.0-or-later
find_path(SicHDF5_INCLUDE_DIR hdf5.h PATH_SUFFIXES hdf5/serial)
find_library(SicHDF5_LIBRARY NAMES hdf5 hdf5_serial PATH_SUFFIXES hdf5/serial)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SicHDF5 REQUIRED_VARS SicHDF5_INCLUDE_DIR SicHDF5_LIBRARY)
if(SicHDF5_FOUND AND NOT TARGET SicHDF5::SicHDF5)
  add_library(SicHDF5::SicHDF5 UNKNOWN IMPORTED)
  set_target_properties(SicHDF5::SicHDF5 PROPERTIES IMPORTED_LOCATION "${SicHDF5_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${SicHDF5_INCLUDE_DIR}")
endif()
