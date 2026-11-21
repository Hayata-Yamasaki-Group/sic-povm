# SPDX-License-Identifier: AGPL-3.0-or-later
add_library(sic_data STATIC "${PROJECT_SOURCE_DIR}/src/data/hoggar.cpp" "${PROJECT_SOURCE_DIR}/src/data/catalogue.cpp")
add_library(sic::data ALIAS sic_data)
set_target_properties(sic_data PROPERTIES EXPORT_NAME data)
target_include_directories(sic_data PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sic_data PUBLIC sic_core)

if(SIC_ENABLE_FLINT)
  target_compile_definitions(sic_data PRIVATE SIC_HAVE_FLINT=1)
  target_sources(sic_data PRIVATE "${PROJECT_SOURCE_DIR}/src/data/numeric.cpp")
  target_link_libraries(sic_data PUBLIC sic_sicx sic_transport)
endif()
install(TARGETS sic_data EXPORT sicTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

if(SIC_ENABLE_FLINT)
 target_sources(sic_data PRIVATE src/data/layout.cpp src/data/decoded_output.cpp)
endif()

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_DATA_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/data/catalogue.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/data/hoggar.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/data/layout.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/data/text_formats.hpp"
  "${PROJECT_SOURCE_DIR}/src/data/catalogue.cpp"
  "${PROJECT_SOURCE_DIR}/src/data/catalogue.hpp"
  "${PROJECT_SOURCE_DIR}/src/data/commands.hpp"
  "${PROJECT_SOURCE_DIR}/src/data/decoded_output.cpp"
  "${PROJECT_SOURCE_DIR}/src/data/decoded_output.hpp"
  "${PROJECT_SOURCE_DIR}/src/data/hoggar.cpp"
  "${PROJECT_SOURCE_DIR}/src/data/layout.cpp"
  "${PROJECT_SOURCE_DIR}/src/data/numeric.cpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)

function(sic_add_data_targets)
if(SIC_ENABLE_FLINT AND TARGET sic-container)
set(SIC_DATA_MULTIPLETS "" CACHE STRING "Multiplets for target exact-data, e.g. d1024/m1;d2048/m1")
set(SIC_DATA_SOURCE "" CACHE STRING "Part source: base URL or local directory (default: the GitHub release named in each parts file)")
set(SIC_DATA_ROOT "${PROJECT_SOURCE_DIR}/../data" CACHE PATH "Repository data tree holding d<d>/m<N>/multiplet.sicx.parts.json")
set(SIC_DATA_JOBS "4" CACHE STRING "Parallel part downloads")
set(SIC_DATA_EXACT_JSON_FACTOR "2" CACHE STRING "Disk estimate for exact.json as a multiple of the SICX size")
if(SIC_DATA_MULTIPLETS)
  set(_dump "$<TARGET_FILE:sic-inspect>")
  set(_outputs "")
  foreach(_m IN LISTS SIC_DATA_MULTIPLETS)
    if(NOT _m MATCHES "^d[0-9]+/m[0-9]+$")
      message(FATAL_ERROR "SIC_DATA_MULTIPLETS entry '${_m}' must look like d1024/m1")
    endif()
    set(_parts "${SIC_DATA_ROOT}/${_m}/multiplet.sicx.parts.json")
    if(NOT EXISTS "${_parts}")
      message(FATAL_ERROR "no parts file at ${_parts}")
    endif()
    set(_out "${CMAKE_BINARY_DIR}/data/${_m}")
    add_custom_command(OUTPUT "${_out}/exact.sicx"
      COMMAND ${CMAKE_COMMAND} -DSICX=$<TARGET_FILE:sic-container> -DDUMP=${_dump} -DSPEC=${_m}
        -DDATA_ROOT=${SIC_DATA_ROOT} -DSOURCE=${SIC_DATA_SOURCE} -DOUT_ROOT=${CMAKE_BINARY_DIR}/data
        -DJOBS=${SIC_DATA_JOBS} -DEXACT_FACTOR=${SIC_DATA_EXACT_JSON_FACTOR}
        -P "${PROJECT_SOURCE_DIR}/../scripts/data/fetch_exact.cmake"
      DEPENDS sic-container sic-inspect "${_parts}"
      COMMENT "exact-data ${_m}: fetch, verify, generate exact.json"
      VERBATIM)
    list(APPEND _outputs "${_out}/exact.sicx")
  endforeach()
  add_custom_target(exact-data DEPENDS ${_outputs})  # not in ALL
endif()

endif()

endfunction()
