# SPDX-License-Identifier: AGPL-3.0-or-later
if(NOT LIBDIR)
  set(LIBDIR lib)
endif()
file(REMOVE_RECURSE "${WORK}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${WORK}/install" --config "${CONFIG}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "install failed: ${o}${e}")
endif()
file(READ "${WORK}/install/${LIBDIR}/cmake/sic/sicTargets.cmake" exports)
if(exports MATCHES "SIC_HAVE_FLINT|SOURCE_ID|sic::sic([^a-zA-Z0-9_]|$)")
  message(FATAL_ERROR "installed package leaks private definitions or an unexpected exported alias")
endif()
# Verify relocation after removing the configured original install prefix.
file(RENAME "${WORK}/install" "${WORK}/relocated")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}" -B "${WORK}/build"
  "-DCMAKE_CXX_FLAGS=${CONSUMER_CXX_FLAGS}" "-DCMAKE_EXE_LINKER_FLAGS=${CONSUMER_LINK_FLAGS}"
  "-DCMAKE_PREFIX_PATH=${WORK}/relocated;${FLINT_PREFIX}" RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer configure failed: ${o}${e}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" --config "${CONFIG}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer build failed: ${o}${e}")
endif()
execute_process(COMMAND "${WORK}/build/core-consumer" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "consumer failed: ${rc}")
endif()

# Compare the relocated installation with the documented public inventory.
if(FLINT)
  set(mode exact)
else()
  set(mode plain)
endif()
execute_process(COMMAND "${NODE}" "${REPO}/scripts/docs/install.mjs"
  "${WORK}/relocated" "${mode}" "${LIBDIR}" "${BINDIR}" "${INCLUDEDIR}" "${APPS}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "installed documentation contract failed: ${out}${err}")
endif()
message(STATUS "${out}")
