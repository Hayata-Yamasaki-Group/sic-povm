# SPDX-License-Identifier: AGPL-3.0-or-later
file(REMOVE_RECURSE "${WORK}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${WORK}/original" RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer install failed")
endif()
file(RENAME "${WORK}/original" "${WORK}/relocated")
file(STRINGS "${BUILD}/CMakeCache.txt" hdf5_include REGEX "^SicHDF5_INCLUDE_DIR:PATH=")
file(STRINGS "${BUILD}/CMakeCache.txt" hdf5_library REGEX "^SicHDF5_LIBRARY:FILEPATH=")
set(dependency_args)
if(hdf5_include AND hdf5_library)
  string(REGEX REPLACE "^[^=]*=" "" hdf5_include "${hdf5_include}")
  string(REGEX REPLACE "^[^=]*=" "" hdf5_library "${hdf5_library}")
  file(MAKE_DIRECTORY "${WORK}/dependencies")
  file(CREATE_LINK "${hdf5_include}" "${WORK}/dependencies/include" SYMBOLIC)
  file(COPY "${hdf5_library}" DESTINATION "${WORK}/dependencies" FOLLOW_SYMLINK_CHAIN)
  get_filename_component(hdf5_name "${hdf5_library}" NAME)
  list(APPEND dependency_args "-DSicHDF5_INCLUDE_DIR=${WORK}/dependencies/include"
       "-DSicHDF5_LIBRARY=${WORK}/dependencies/${hdf5_name}")
endif()
file(GLOB target_files "${WORK}/relocated/lib*/cmake/sic/sicTargets*.cmake")
foreach(target_file IN LISTS target_files)
  file(READ "${target_file}" target_text)
  if(target_text MATCHES "/usr/[^\"]*hdf5|/home/|SOURCE_ID")
    message(FATAL_ERROR "nonrelocatable/private installed interface")
  endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}" -B "${WORK}/build"
  "-DCMAKE_CXX_FLAGS=${CONSUMER_CXX_FLAGS}" "-DCMAKE_EXE_LINKER_FLAGS=${CONSUMER_LINK_FLAGS}"
  "-DCMAKE_PREFIX_PATH=${WORK}/relocated;${FLINT_PREFIX}" ${dependency_args} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer configure failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer build failed")
endif()
find_program(ENV_PROGRAM env REQUIRED)
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${WORK}/build/sic-test-install-policy" "${FIXTURE}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer run failed")
endif()

# The relocated installed producer must use its pinned d2 record independently of cwd.
file(MAKE_DIRECTORY "${WORK}/empty")
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1"
  "${WORK}/relocated/bin/sic-construct" 1 --threads 1
  --out "${WORK}/empty/d2.sicx" WORKING_DIRECTORY "${WORK}/empty" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "relocated installed d2 construction failed")
endif()

# Exercise all three installed tools from a directory outside the source tree.
set(data "${WORK}/relocated/${DATADIR}/sic")
set(bin "${WORK}/relocated/${BINDIR}")
set(ray "${data}/data/d2/m1/c0.csv")
if(EXISTS "${data}/cpp")
  message(FATAL_ERROR "test fixtures were installed")
endif()
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-identify" "${ray}"
  WORKING_DIRECTORY "${WORK}/empty" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "MATCH d2-m1-c0")
  message(FATAL_ERROR "relocated identify failed: ${out}${err}")
endif()
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-stabilizer" --dim 2
  --out "${WORK}/empty/stabilizer.txt" WORKING_DIRECTORY "${WORK}/empty"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "relocated stabilizer failed: ${out}${err}")
endif()
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-certify-stabilizer"
  "${WORK}/empty/stabilizer.txt" WORKING_DIRECTORY "${WORK}/empty"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "PASS")
  message(FATAL_ERROR "relocated stabilizer certification failed: ${out}${err}")
endif()
file(STRINGS "${DEPENDENCY_PATHS}" dependency_paths)
execute_process(COMMAND "${NODE}" "${SCAN}" "${WORK}/relocated" "${REPO}" "${BUILD}" --dependencies ${dependency_paths}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "installed path scan failed: ${out}${err}")
endif()
file(WRITE "${WORK}/relocated/path-scan-negative-control" "${DEPENDENCY_CONTROL}/arf.h")
execute_process(COMMAND "${NODE}" "${SCAN}" "${WORK}/relocated" "${REPO}" "${BUILD}" --dependencies ${dependency_paths}
  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
file(REMOVE "${WORK}/relocated/path-scan-negative-control")
if(rc EQUAL 0)
  message(FATAL_ERROR "path scan negative control was not detected")
endif()

# FILE mode uses its caller input independently of catalogue lookup.
file(COPY "${ray}" DESTINATION "${WORK}/empty")
set(ray "${WORK}/empty/c0.csv")
file(RENAME "${data}/data" "${data}/data-hidden")
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-stabilizer" "${ray}"
  WORKING_DIRECTORY "${WORK}/empty" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(RENAME "${data}/data-hidden" "${data}/data")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "stabilizer FILE mode unexpectedly needs data: ${out}${err}")
endif()

# Explicit --repo takes precedence even when an installed catalogue exists.
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-stabilizer" --dim 2 --repo "${WORK}/absent"
  WORKING_DIRECTORY "${WORK}/empty" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "explicit --repo was ignored")
endif()
# Hide installed data, prove the cwd fallback and missing-data diagnostic.
file(MAKE_DIRECTORY "${WORK}/cwd")
file(COPY "${data}/data" DESTINATION "${WORK}/cwd")
file(RENAME "${data}/data" "${data}/data-hidden")
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-stabilizer" --dim 2
  WORKING_DIRECTORY "${WORK}/cwd" RESULT_VARIABLE fallback_rc OUTPUT_QUIET ERROR_VARIABLE fallback_err)
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-stabilizer" --dim 2
  WORKING_DIRECTORY "${WORK}/empty" RESULT_VARIABLE missing_rc OUTPUT_VARIABLE missing_out ERROR_VARIABLE missing_err)
execute_process(COMMAND "${ENV_PROGRAM}" -i "SIC_THREADS=1" "${bin}/sic-stabilizer" --dim 2 --tol 0
  WORKING_DIRECTORY "${WORK}/empty" RESULT_VARIABLE invalid_rc OUTPUT_VARIABLE invalid_out ERROR_VARIABLE invalid_err)
file(RENAME "${data}/data-hidden" "${data}/data")
if(NOT invalid_rc EQUAL 2 OR NOT "${invalid_out}${invalid_err}" MATCHES "usage: ")
  message(FATAL_ERROR "invalid options did not take precedence over data lookup")
endif()
if(NOT fallback_rc EQUAL 0)
  message(FATAL_ERROR "cwd data fallback failed: ${fallback_err}")
endif()
if(missing_rc EQUAL 0 OR NOT "${missing_out}${missing_err}" MATCHES "--repo")
  message(FATAL_ERROR "missing data diagnostic did not name --repo")
endif()
