# SPDX-License-Identifier: AGPL-3.0-or-later
# The numeric/core library and its installed package never link libcurl.
find_package(CURL 7.81 QUIET)
option(SIC_ENABLE_REMOTE "Enable HTTP Range access to SICX transport parts" ${CURL_FOUND})
if(SIC_ENABLE_REMOTE AND NOT CURL_FOUND)
  message(FATAL_ERROR "SIC_ENABLE_REMOTE requires libcurl >= 7.81 and development headers")
endif()
if(NOT SIC_ENABLE_FLINT)
  return()
endif()
add_library(sic_access STATIC
  src/access/remote.cpp
  src/access/http_transfer.cpp
  src/access/range_cache.cpp
  src/access/range_plan.cpp)
target_include_directories(sic_access PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sic_access PUBLIC sic_sicx sic_transport)
add_library(sic::access ALIAS sic_access)
set_target_properties(sic_access PROPERTIES EXPORT_NAME access)
target_compile_options(sic_access PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)
if(SIC_ENABLE_REMOTE)
  sic_map_dependency(CURL::libcurl ${CURL_INCLUDE_DIRS})
  get_target_property(_sic_curl_map CURL::libcurl INTERFACE_COMPILE_OPTIONS)
  target_compile_options(sic_access PRIVATE ${_sic_curl_map})
  foreach(_config "" _RELEASE _DEBUG _RELWITHDEBINFO _MINSIZEREL)
    get_target_property(_curl_location CURL::libcurl IMPORTED_LOCATION${_config})
    if(_curl_location MATCHES "[.]a$")
      message(FATAL_ERROR "SIC_ENABLE_REMOTE requires shared libcurl for lazy loading; disable it or select shared libcurl")
    endif()
  endforeach()
  # dlopen preserves the established RSS of local decoding; only HTTP loads curl.
  target_link_libraries(sic_access PRIVATE ${CMAKE_DL_LIBS})
  target_include_directories(sic_access PRIVATE ${CURL_INCLUDE_DIRS})
  set(SIC_CURL_SONAME "libcurl.so.4" CACHE STRING "Runtime libcurl loader name (resolved on the consumer host)")
  if(SIC_CURL_SONAME MATCHES "[/\\]")
    message(FATAL_ERROR "SIC_CURL_SONAME must be a loader name, not a build-host path")
  endif()
  target_compile_definitions(sic_access PRIVATE SIC_HAVE_CURL=1
    SIC_CURL_LIBRARY="${SIC_CURL_SONAME}")
endif()

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_SICXREMOTE_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/access/remote.hpp"
  "${PROJECT_SOURCE_DIR}/src/access/http_transfer.cpp"
  "${PROJECT_SOURCE_DIR}/src/access/http_transfer.hpp"
  "${PROJECT_SOURCE_DIR}/src/access/range_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/access/range_cache.hpp"
  "${PROJECT_SOURCE_DIR}/src/access/range_plan.cpp"
  "${PROJECT_SOURCE_DIR}/src/access/range_plan.hpp"
  "${PROJECT_SOURCE_DIR}/src/access/remote.cpp"
  "${PROJECT_SOURCE_DIR}/src/access/remote_state.hpp"
  "${PROJECT_SOURCE_DIR}/src/access/source_arguments.cpp"
  "${PROJECT_SOURCE_DIR}/src/access/source_arguments.hpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)
