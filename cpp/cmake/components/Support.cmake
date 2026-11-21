add_library(sic_support STATIC
  src/support/arithmetic/limits.cpp
  src/support/text/error.cpp
  src/support/text/tool_version.cpp
  src/support/sha256.cpp
  src/support/io/byte_source.cpp
  src/support/io/durable_file.cpp
  src/support/text/cli.cpp
  src/support/text/json.cpp
  src/support/text/hex.cpp
  src/support/runtime/admission.cpp
  src/support/runtime/kernel_settings.cpp
  src/support/runtime/stop.cpp
  src/support/runtime/thread_group.cpp
)
add_library(sic::support ALIAS sic_support)
set_target_properties(sic_support PROPERTIES EXPORT_NAME support)
target_compile_features(sic_support PUBLIC cxx_std_20)
target_include_directories(sic_support PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(sic_support PUBLIC Threads::Threads)


target_include_directories(sic_support PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(SIC_ENABLE_FLINT)
  target_sources(sic_support PRIVATE
    src/support/lattice_coordinates.cpp
    src/support/arithmetic/ball_vectors.cpp
    src/support/arithmetic/flint_raii.cpp
    src/support/runtime/worker_team.cpp
    src/support/runtime/worker_context.cpp
    src/support/admitted_worker_team.cpp)
  target_link_libraries(sic_support PRIVATE SicFLINT::SicFLINT)
endif()

target_sources(sic_support PRIVATE src/support/resources/numa_policy.cpp)

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_SUPPORT_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/limits.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/arithmetic/limits.cpp"
  "${PROJECT_SOURCE_DIR}/include/sic/error.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/text/error.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/text/tool_version.cpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/admission.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/ball_vectors.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/byte_source.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/cli.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/parse_number.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/durable_file.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/flint_raii.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/json.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/hex.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/kernel_settings.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/stop.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/thread_group.hpp"
  "${PROJECT_SOURCE_DIR}/include/sic/support/tool_version.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/arithmetic/ball_vectors.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/arithmetic/flint_raii.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/index.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/io/byte_source.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/io/durable_file.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/io/durable_output.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/io/write_all.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/lattice_coordinates.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/lattice_coordinates.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/allocation_forecast.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/allocation_forecast.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/checked_bytes.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/checked_bytes.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/numa_policy.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/numa_policy.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/platform_snapshot.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/platform_snapshot.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/snapshot.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/resources/resource_units.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/admission.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/kernel_settings.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/stop.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/thread_group.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/worker_team.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/worker_team.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/worker_context.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/worker_context.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/admitted_worker_team.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/admitted_worker_team.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/sha256.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/text/cli.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/text/json.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/text/hex.cpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)

if(SIC_ENABLE_FLINT)
 target_sources(sic_support PRIVATE src/support/resources.cpp src/support/resources/checked_bytes.cpp src/support/resources/platform_snapshot.cpp src/support/resources/allocation_forecast.cpp)
endif()

if(SIC_ENABLE_FLINT)
 target_sources(sic_support PRIVATE
  "${PROJECT_SOURCE_DIR}/src/support/arithmetic/balanced_product.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/periodic_grid.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/reservations.cpp"
 )
endif()
list(APPEND SIC_SUPPORT_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/src/support/arithmetic/balanced_product.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/arithmetic/balanced_product.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/periodic_grid.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/periodic_grid.cpp"
  "${PROJECT_SOURCE_DIR}/src/support/periodic_grid.tpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/reservations.hpp"
  "${PROJECT_SOURCE_DIR}/src/support/runtime/reservations.cpp"
)
