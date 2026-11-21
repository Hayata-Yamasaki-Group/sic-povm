# SPDX-License-Identifier: AGPL-3.0-or-later
# Register before every subdirectory/target install rule, including apps.
if(SIC_ENABLE_TEST_HOOKS)
  if(CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR "SIC_ENABLE_TEST_HOOKS requires CMake >=3.21 for ALL_COMPONENTS install refusal")
  endif()
  install(CODE "message(FATAL_ERROR \"Installation refused: SIC_ENABLE_TEST_HOOKS=ON; configure a separate OFF production tree\")" ALL_COMPONENTS)
endif()
