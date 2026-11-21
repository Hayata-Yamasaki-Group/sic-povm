# SPDX-License-Identifier: AGPL-3.0-or-later
include(CMakePackageConfigHelpers)
set(_sic_public_targets sic_support sic_core sic_search)
set_target_properties(sic_search PROPERTIES EXPORT_NAME search)
install(DIRECTORY include/sic/search/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/search" FILES_MATCHING PATTERN "*.hpp")
set_target_properties(sic_core PROPERTIES EXPORT_NAME core)
install(DIRECTORY include/sic/core/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/core" FILES_MATCHING PATTERN "*.hpp")
install(FILES include/sic/support/parse_number.hpp include/sic/support/cli.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES include/sic/error.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic")
install(FILES include/sic/limits.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic")
install(FILES include/sic/support/stop.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES include/sic/support/byte_source.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES include/sic/support/json.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES include/sic/support/durable_file.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES include/sic/support/admission.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
if(SIC_ENABLE_FLINT)
  list(APPEND _sic_public_targets sic_bounds sic_sicx sic_transport sic_decode sic_access)
  include("${CMAKE_CURRENT_LIST_DIR}/PublicSicxHeaders.cmake")
  install(FILES include/sic/bounds/overlap_bounds.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/bounds")
endif()
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../dependencies/FindSicHDF5.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sic")
set(_sic_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/sic")
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/sicConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/sicConfig.cmake"
  INSTALL_DESTINATION "${_sic_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/sicConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(TARGETS ${_sic_public_targets} EXPORT sicTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(EXPORT sicTargets NAMESPACE sic:: DESTINATION "${_sic_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sicConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/sicConfigVersion.cmake"
  DESTINATION "${_sic_package_dir}")
if(SIC_ENABLE_FLINT)
  install(FILES "${CMAKE_CURRENT_LIST_DIR}/../dependencies/FindSicFLINT.cmake" DESTINATION "${_sic_package_dir}")
endif()

foreach(_tool sic-check sic-overlaps sic-identify sic-stabilizer sic-certify-stabilizer
              sic-construct sic-decode sic-verify sic-inspect sic-container sic-verify-independent sic-certify-norm sic-certify-crt sic-check-crt-input)
  if(TARGET ${_tool})
    install(TARGETS ${_tool} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  endif()
endforeach()
if(SIC_BUILD_TESTS AND NOT SIC_ENABLE_TEST_HOOKS)
  add_test(NAME sic_test_support_core_installed_consumer
    COMMAND ${CMAKE_COMMAND} -DBUILD=${PROJECT_BINARY_DIR}
      -DSOURCE=${PROJECT_SOURCE_DIR}/tests/install/core
      -DWORK=${PROJECT_BINARY_DIR}/core-consumer-test
      "-DFLINT_PREFIX=${CMAKE_PREFIX_PATH}" "-DCONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
      "-DCONSUMER_LINK_FLAGS=${CMAKE_EXE_LINKER_FLAGS}" -DLIBDIR=${CMAKE_INSTALL_LIBDIR} -DCONFIG=$<CONFIG>
      -DNODE=${SIC_NODE_EXECUTABLE} -DREPO=${SIC_REPO_ROOT} -DFLINT=${SIC_ENABLE_FLINT} -DAPPS=${SIC_BUILD_APPS}
      -DBINDIR=${CMAKE_INSTALL_BINDIR} -DINCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}
      -P ${PROJECT_SOURCE_DIR}/tests/install/core/run.cmake)
  set_tests_properties(sic_test_support_core_installed_consumer PROPERTIES LABELS "integration;policy" TIMEOUT 120)
endif()

install(FILES include/sic/data/catalogue.hpp include/sic/data/hoggar.hpp include/sic/data/text_formats.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/data")
if(SIC_ENABLE_FLINT)
 install(FILES include/sic/data/layout.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/data")
endif()

# Release data is self-contained; numerical regression fixtures are never installed.
install(DIRECTORY "${SIC_REPO_ROOT}/data/" DESTINATION "${CMAKE_INSTALL_DATADIR}/sic/data")
