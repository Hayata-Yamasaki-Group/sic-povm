# SPDX-License-Identifier: AGPL-3.0-or-later
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
sic_add_executable(sic-check "${PROJECT_SOURCE_DIR}/apps/check/main.cpp")
sic_add_executable(read-fiducial "${PROJECT_SOURCE_DIR}/examples/read_fiducial.cpp")
if(SIC_ENABLE_FLINT)
  target_link_libraries(read-fiducial PRIVATE SicFLINT::SicFLINT)
  target_compile_definitions(read-fiducial PRIVATE SIC_HAVE_FLINT=1)
endif()

get_filename_component(SIC_REPO_ROOT "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)
sic_add_executable(sic-overlaps "${PROJECT_SOURCE_DIR}/apps/overlaps/main.cpp")
sic_add_executable(sic-identify "${PROJECT_SOURCE_DIR}/apps/identify/main.cpp")
target_link_libraries(sic-identify PRIVATE sic_data sic_search)
sic_add_executable(sic-stabilizer "${PROJECT_SOURCE_DIR}/apps/stabilizer/main.cpp")
target_link_libraries(sic-stabilizer PRIVATE sic_data sic_search)
foreach(_app sic-identify sic-stabilizer)
  target_compile_definitions(${_app} PRIVATE SIC_DATA_ROOT_FROM_BINDIR="${SIC_DATA_ROOT_FROM_BINDIR}")
endforeach()

target_link_libraries(sic-identify PRIVATE sic::search)
target_link_libraries(sic-stabilizer PRIVATE sic::search)


if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  sic_add_sicx_app(sic-verify "${PROJECT_SOURCE_DIR}/apps/verify/main.cpp")
  target_link_libraries(sic-verify PRIVATE sic_access sic_core)
  sic_add_sicx_app(sic-inspect "${PROJECT_SOURCE_DIR}/apps/inspect/main.cpp")
  target_link_libraries(sic-inspect PRIVATE sic_data sic_transport)
  sic_add_sicx_app(sic-decode "${PROJECT_SOURCE_DIR}/apps/decode/main.cpp")
  target_link_libraries(sic-decode PRIVATE sic_access sic_data sic_decode)
  target_include_directories(sic-decode PRIVATE "${PROJECT_BINARY_DIR}/generated")
  sic_add_sicx_app(sic-container "${PROJECT_SOURCE_DIR}/apps/container/main.cpp")
  target_link_libraries(sic-container PRIVATE sic_access sic_construct sic_exact sic_transport)
  add_executable(sic-construct "${PROJECT_SOURCE_DIR}/apps/construct/main.cpp")
  target_include_directories(sic-construct PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(sic-construct PRIVATE sic_construct sic_core sic_exact)
endif()

if(SIC_ENABLE_FLINT)
add_executable(sic-data "${PROJECT_SOURCE_DIR}/apps/data/main.cpp")
target_link_libraries(sic-data PRIVATE sic_data)
target_include_directories(sic-data PRIVATE "${PROJECT_SOURCE_DIR}/src")

add_executable(sic-check-balls "${PROJECT_SOURCE_DIR}/apps/check_balls/main.cpp")
target_link_libraries(sic-check-balls PRIVATE sic_bounds)
target_include_directories(sic-check-balls PRIVATE "${PROJECT_SOURCE_DIR}/src")
install(TARGETS sic-data sic-check-balls RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

endif()

if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  foreach(_tool sic-verify sic-container sic-decode)
    target_sources(${_tool} PRIVATE "${PROJECT_SOURCE_DIR}/src/access/source_arguments.cpp")
  endforeach()
endif()

if(TARGET sic-identify)
  target_sources(sic-identify PRIVATE "${PROJECT_SOURCE_DIR}/apps/identify/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/identify/command.cpp")
endif()

if(TARGET sic-overlaps)
  target_sources(sic-overlaps PRIVATE "${PROJECT_SOURCE_DIR}/apps/overlaps/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/overlaps/command.cpp")
endif()

if(TARGET sic-stabilizer)
  target_sources(sic-stabilizer PRIVATE "${PROJECT_SOURCE_DIR}/apps/stabilizer/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/stabilizer/command.cpp")
endif()

if(TARGET sic-verify)
  target_sources(sic-verify PRIVATE "${PROJECT_SOURCE_DIR}/apps/verify/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/verify/command.cpp")
endif()

if(TARGET sic-inspect)
  target_sources(sic-inspect PRIVATE "${PROJECT_SOURCE_DIR}/apps/inspect/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/inspect/command.cpp")
endif()

if(TARGET sic-decode)
  target_sources(sic-decode PRIVATE "${PROJECT_SOURCE_DIR}/apps/decode/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/decode/command.cpp")
endif()

target_link_libraries(sic-stabilizer PRIVATE sic_data)
if(TARGET sic-decode)
 target_link_libraries(sic-decode PRIVATE sic_data)
endif()

if(TARGET sic-container)
 target_sources(sic-container PRIVATE "${PROJECT_SOURCE_DIR}/apps/container/command_line.cpp" "${PROJECT_SOURCE_DIR}/apps/container/command.cpp")
endif()

target_link_libraries(sic-identify PRIVATE sic_data)
if(TARGET sic-inspect)
 target_link_libraries(sic-inspect PRIVATE sic_data)
endif()

if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
foreach(app sic-decode sic-verify sic-container)
  target_link_libraries(${app} PRIVATE sic_access)
endforeach()

endif()
