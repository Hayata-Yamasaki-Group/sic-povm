# SPDX-License-Identifier: AGPL-3.0-or-later
include(CheckIPOSupported)
check_ipo_supported(RESULT _sic_ipo_supported OUTPUT _sic_ipo_error LANGUAGES CXX)
# Enable IPO by default for supported Release production and independent checker targets.
option(SIC_ENABLE_LTO "Optimize Release production and independent checker targets at link time" ON)

function(sic_apply_link_time_optimization)
  # Do not set the global CMAKE_INTERPROCEDURAL_OPTIMIZATION initializer:
  # each checker is optimized only within its own executable boundary.
  set(_enabled ${SIC_ENABLE_LTO})
  set(_sanitized FALSE)
  if(NOT _sic_ipo_supported)
    set(_enabled OFF)
    message(STATUS "SIC LTO: OFF (IPO unavailable: ${_sic_ipo_error})")
  else()
    # Sanitizer builds retain their existing instrumentation and link behavior.
    get_cmake_property(_variables VARIABLES)
    foreach(_variable IN LISTS _variables)
      if(_variable MATCHES "^CMAKE_(CXX|EXE_LINKER|SHARED_LINKER)_FLAGS" AND
         "${${_variable}}" MATCHES "-fsanitize=")
        set(_enabled OFF)
        set(_sanitized TRUE)
      endif()
    endforeach()
    message(STATUS "SIC LTO: ${_enabled} (Release/RelWithDebInfo production and checkers; sanitizer=${_sanitized})")
  endif()
  _sic_project_targets("${PROJECT_SOURCE_DIR}" _targets)
  foreach(_target IN LISTS _targets)
    get_target_property(_type ${_target} TYPE)
    get_target_property(_source ${_target} SOURCE_DIR)
    get_target_property(_checker ${_target} SIC_INDEPENDENT_DIRECTORY)
    if((_target MATCHES "^sic_" AND NOT _target MATCHES "^sic[-_]test[-_]" AND
        _type MATCHES "^(STATIC|SHARED|OBJECT)_LIBRARY$") OR
       (_target MATCHES "^sic-" AND _source STREQUAL "${PROJECT_SOURCE_DIR}/apps") OR
       (_checker AND _type STREQUAL "EXECUTABLE" AND NOT _target MATCHES "^sic[-_]test[-_]"))
      set(_target_enabled ${_enabled})
      get_target_property(_inline_only ${_target} SIC_CHECKER_INLINE_ONLY)
      if(_checker AND _inline_only)
        set(_target_enabled OFF)
      endif()
      set_target_properties(${_target} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION FALSE
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ${_target_enabled}
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ${_target_enabled})
      if(_target_enabled AND _type STREQUAL "STATIC_LIBRARY" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Native code alongside GCC IR keeps installed archives usable with
        # -fno-lto (and by consumers without the GCC linker plugin).
        target_compile_options(${_target} PRIVATE
          "$<$<CONFIG:Release,RelWithDebInfo>:-ffat-lto-objects>")
      endif()
    endif()
  endforeach()
endfunction()
