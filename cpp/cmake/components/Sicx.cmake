# SPDX-License-Identifier: AGPL-3.0-or-later
# SICX model, codecs and authenticated container.
if(NOT SIC_ENABLE_FLINT)
  return()
endif()
add_library(sic_sicx STATIC
  src/sicx/codec/ball.cpp
  src/sicx/codec/block.cpp
  src/sicx/codec/integer.cpp
  src/sicx/codec/ray_list.cpp
  src/sicx/codec/rescale.cpp
  src/sicx/codec/rescaling_rule.cpp
  src/sicx/codec/unrescaled.cpp
  src/sicx/container/directory.cpp
  src/sicx/container/finalize.cpp
  src/sicx/io/io.cpp
  src/sicx/container/io.cpp
  src/sicx/container/json.cpp
  src/sicx/container/leaf_index.cpp
  src/sicx/container/part_append.cpp
  src/sicx/container/reader.cpp
  src/sicx/container/rescaling_input.cpp
  src/sicx/container/stream_writer.cpp
  src/sicx/container/verify.cpp
  src/sicx/exact/conjugation.cpp
  src/sicx/exact/element_json.cpp
  src/sicx/exact/embedding.cpp
  src/sicx/exact/exact.cpp
  src/sicx/exact/tower.cpp
  src/sicx/model/branches.cpp
  src/sicx/model/format.cpp
  src/sicx/model/metadata.cpp
  src/sicx/model/unit_group.cpp
  src/sicx/text/text.cpp
)
add_library(sic::sicx ALIAS sic_sicx)
set_target_properties(sic_sicx PROPERTIES EXPORT_NAME sicx)
target_include_directories(sic_sicx PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sic_sicx PUBLIC sic_support SicFLINT::SicFLINT)
function(sic_add_sicx_app name source)
  add_executable(${name} ${source})
  if(SIC_BUILD_TESTS AND name MATCHES "^sic[-_]test[-_]")
    target_link_libraries(${name} PRIVATE sic_test_assertions)
  endif()
  target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(${name} PRIVATE sic_sicx)
  target_compile_options(${name} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)
endfunction()


if(SIC_BUILD_TESTS)
  include("${PROJECT_SOURCE_DIR}/tests/sicx/tests.cmake")
endif()

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_SICX_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/branches.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/exact.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/finalize.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/format.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/io.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/json.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/rescaling_rule.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/text.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/sicx/verify.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/ball.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/ball.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/block.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/block.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/block.tpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/integer.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/integer.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/ray_list.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/ray_list.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/rescale.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/rescale.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/rescaling_rule.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/rescaling_rule.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/unrescaled.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/codec/unrescaled.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/directory.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/directory.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/finalize.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/finalization_builder.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/io/io.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/io.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/io/io.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/json.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/leaf_index.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/leaf_index.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/part_append.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/producer_writer.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/producer_writer.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/reader.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/reader.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/rescaling_input.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/rescaling_input.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/stream_writer.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/stream_writer.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/verify.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/verify.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/container/verify.tpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/conjugation.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/conjugation.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/element_json.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/element_json.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/embedding.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/embedding.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/exact.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/exact.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/tower.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/exact/tower.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/branches.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/branches.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/branches.tpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/format.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/metadata.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/metadata.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/unit_group.cpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/model/unit_group.hpp"
  "${PROJECT_SOURCE_DIR}/src/sicx/text/text.cpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)

target_sources(sic_sicx PRIVATE src/sicx/container/producer_writer.cpp)
