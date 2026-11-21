# SPDX-License-Identifier: AGPL-3.0-or-later
# Resolve native dependencies where the package is consumed, never at export.
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FLINT QUIET IMPORTED_TARGET flint>=3)
endif()
find_path(SicFLINT_INCLUDE_DIR NAMES flint/flint.h HINTS ${FLINT_INCLUDE_DIRS})
find_library(SicFLINT_LIBRARY NAMES flint HINTS ${FLINT_LIBRARY_DIRS})
if(SicFLINT_INCLUDE_DIR)
  file(STRINGS "${SicFLINT_INCLUDE_DIR}/flint/flint.h" _sic_flint_version_line
    REGEX "^#define FLINT_VERSION +\"")
  string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" SicFLINT_VERSION "${_sic_flint_version_line}")
endif()
if(NOT TARGET PkgConfig::FLINT)
  find_library(SicFLINT_GMP_LIBRARY NAMES gmp)
  find_library(SicFLINT_MPFR_LIBRARY NAMES mpfr)
  set(_sic_flint_extra_required SicFLINT_GMP_LIBRARY SicFLINT_MPFR_LIBRARY)
endif()
find_package_handle_standard_args(SicFLINT
  REQUIRED_VARS SicFLINT_LIBRARY SicFLINT_INCLUDE_DIR SicFLINT_VERSION ${_sic_flint_extra_required}
  VERSION_VAR SicFLINT_VERSION)
if(SicFLINT_FOUND AND NOT TARGET SicFLINT::SicFLINT)
  add_library(SicFLINT::SicFLINT INTERFACE IMPORTED)
  set_target_properties(SicFLINT::SicFLINT PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SicFLINT_INCLUDE_DIR}")
  if(TARGET PkgConfig::FLINT)
    set_property(TARGET SicFLINT::SicFLINT PROPERTY INTERFACE_LINK_LIBRARIES
      "${SicFLINT_LIBRARY};PkgConfig::FLINT")
  else()
    set_property(TARGET SicFLINT::SicFLINT PROPERTY INTERFACE_LINK_LIBRARIES
      "${SicFLINT_LIBRARY};${SicFLINT_MPFR_LIBRARY};${SicFLINT_GMP_LIBRARY}")
  endif()
endif()
set(FLINT_LIBRARY "${SicFLINT_LIBRARY}")
mark_as_advanced(SicFLINT_INCLUDE_DIR SicFLINT_LIBRARY SicFLINT_GMP_LIBRARY SicFLINT_MPFR_LIBRARY)
