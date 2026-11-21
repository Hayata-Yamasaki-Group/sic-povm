# SPDX-License-Identifier: AGPL-3.0-or-later
# Private quarantine publication also supports allocator-independent plain tests.
add_library(sic_construct_output STATIC src/construct/output/quarantine.cpp)
target_include_directories(sic_construct_output PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sic_construct_output PRIVATE sic_support)
