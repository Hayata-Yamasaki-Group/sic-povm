# SPDX-License-Identifier: AGPL-3.0-or-later
add_library(sic_search STATIC src/search/clifford.cpp)
add_library(sic::search ALIAS sic_search)
target_link_libraries(sic_search PUBLIC sic_core)

target_sources(sic_search PRIVATE src/search/stabilizer.cpp src/search/catalogue_match.cpp)
target_include_directories(sic_search PRIVATE "${PROJECT_SOURCE_DIR}/src")

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_SEARCH_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/search/clifford.hpp"
  "${PROJECT_SOURCE_DIR}/src/search/catalogue_match.cpp"
  "${PROJECT_SOURCE_DIR}/src/search/catalogue_match.hpp"
  "${PROJECT_SOURCE_DIR}/src/search/clifford.cpp"
  "${PROJECT_SOURCE_DIR}/src/search/stabilizer.cpp"
  "${PROJECT_SOURCE_DIR}/src/search/stabilizer.hpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)
