# SPDX-License-Identifier: AGPL-3.0-or-later
# d = 2 uses the native closed form at s = 1.
  set(SIC_DECODER_SOURCE_COMMIT "" CACHE STRING "Full source commit for exported decoder source builds (otherwise Git HEAD)")
  set(_sic_decoder_commit "${SIC_DECODER_SOURCE_COMMIT}")
  if(_sic_decoder_commit STREQUAL "" AND EXISTS "${PROJECT_SOURCE_DIR}/SOURCE_COMMIT")
    file(READ "${PROJECT_SOURCE_DIR}/SOURCE_COMMIT" _sic_decoder_commit)
    string(STRIP "${_sic_decoder_commit}" _sic_decoder_commit)
  endif()
  if(_sic_decoder_commit STREQUAL "")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse HEAD
      OUTPUT_VARIABLE _sic_decoder_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    # Reconfigure when the checkout advances.
    foreach(_ref HEAD)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --git-path "${_ref}"
        OUTPUT_VARIABLE _sic_decoder_ref OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
      get_filename_component(_sic_decoder_ref "${_sic_decoder_ref}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sic_decoder_ref}")
    endforeach()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" symbolic-ref -q HEAD
      OUTPUT_VARIABLE _sic_decoder_branch OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _sic_decoder_branch STREQUAL "")
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --git-path "${_sic_decoder_branch}"
        OUTPUT_VARIABLE _sic_decoder_ref OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
      get_filename_component(_sic_decoder_ref "${_sic_decoder_ref}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sic_decoder_ref}")
    endif()
  endif()
  string(LENGTH "${_sic_decoder_commit}" _sic_decoder_commit_length)
  if(NOT _sic_decoder_commit MATCHES "^[0-9a-f]+$" OR
      NOT (_sic_decoder_commit_length EQUAL 40 OR _sic_decoder_commit_length EQUAL 64))
    message(FATAL_ERROR "SIC_DECODER_SOURCE_COMMIT must be a full 40- or 64-digit lowercase Git commit")
  endif()
  configure_file("${PROJECT_SOURCE_DIR}/src/decode/build_identity.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/sicx_decode/build_identity.hpp" @ONLY)


add_library(sic_decode STATIC
  "${PROJECT_SOURCE_DIR}/src/decode/contraction.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/dag.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/decoder_state.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/element_source.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/leaf_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/anchor.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/kernels.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/model.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/placement.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/placement.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/pool.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/prefix/prefix_walk.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/scheduling.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/body_plan.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/body_admission.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/body_dispatch.cpp"
)
add_library(sic::decode ALIAS sic_decode)
set_target_properties(sic_decode PROPERTIES EXPORT_NAME decode)
target_link_libraries(sic_decode PUBLIC sic_sicx)
target_include_directories(sic_decode PRIVATE "${PROJECT_SOURCE_DIR}/src")

# Complete compiled decoder closure, including private inline definitions.
set(SIC_DECODE_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/anchor.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/anchor.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/decode/decoder.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/decode/placement.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/build_identity.hpp.in"
  "${PROJECT_SOURCE_DIR}/src/decode/contraction.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/contraction.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/contraction.inl.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/dag.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/dag.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/decoder.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/decoder_state.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/element_source.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/element_source.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/element_source.tpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/leaf_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/input/leaf_values.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/kernels.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/kernels.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/kernels/kernels.inl.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/model.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/model.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/placement.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/placement.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/pool.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/pool.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/pool.tpp"
  "${PROJECT_SOURCE_DIR}/src/decode/prefix/prefix_walk.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/prefix/prefix_walk.hpp"
  "${PROJECT_SOURCE_DIR}/src/decode/prefix/prefix_walk.tpp"
  "${PROJECT_SOURCE_DIR}/src/decode/scheduling.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/body_plan.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/body_admission.cpp"
  "${PROJECT_SOURCE_DIR}/src/decode/body_dispatch.cpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)
