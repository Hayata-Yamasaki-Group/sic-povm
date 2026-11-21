# SPDX-License-Identifier: AGPL-3.0-or-later
add_library(sic_transport STATIC src/transport/parts.cpp)
add_library(sic::transport ALIAS sic_transport)
set_target_properties(sic_transport PROPERTIES EXPORT_NAME transport)
target_link_libraries(sic_transport PUBLIC sic_support sic_sicx)

target_include_directories(sic_transport PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_sources(sic_transport PRIVATE src/transport/fetch.cpp src/transport/repair.cpp)

# Stable content identity of this component, including compiled bodies and callback templates.
set(SIC_TRANSPORT_IDENTITY_INPUTS
  "${PROJECT_SOURCE_DIR}/include/sic/transport/parts.hpp"
  "${PROJECT_SOURCE_DIR}/src/transport/fetch.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/fetch.hpp"
  "${PROJECT_SOURCE_DIR}/src/transport/parts.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/parts.hpp"
  "${PROJECT_SOURCE_DIR}/src/transport/repair.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/repair.hpp"
  "${CMAKE_CURRENT_LIST_FILE}"
)
