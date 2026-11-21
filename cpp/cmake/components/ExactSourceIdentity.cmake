# SPDX-License-Identifier: AGPL-3.0-or-later
# Source identities of the exact constructor headers (cache/checkpoint compatibility keys).
#
# A leading "// SPDX-License-Identifier: ..." line is excluded from the digest, so the group and
# table identities equal those of the frozen candidate headers that had no license line (the
# representative-fast-v3 build: table 20350bc1...). Every other byte counts.

set(_sic_spdx_line "// SPDX-License-Identifier: AGPL-3.0-or-later\n")
string(LENGTH "${_sic_spdx_line}" _sic_spdx_length)

# sic_source_digest(<file> <out-var>): SHA-256 of the file content without its SPDX line.
function(sic_source_digest file out)
  file(READ "${file}" _content)
  string(FIND "${_content}" "${_sic_spdx_line}" _at)
  if(_at EQUAL 0)
    string(SUBSTRING "${_content}" ${_sic_spdx_length} -1 _content)
  endif()
  string(SHA256 _digest "${_content}")
  set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  set(${out} "${_digest}" PARENT_SCOPE)
endfunction()

# sic_source_identity(<out-var> <prefix> <file>...): "<prefix><digest>:<digest>:..." in argument order.
function(sic_source_identity out prefix)
  # Effective instrumentation is part of every cache identity, including source exports.
  set(_id "${prefix}test_hooks=${SIC_TEST_HOOKS_MODE}:")
  foreach(_file IN LISTS ARGN)
    sic_source_digest("${_file}" _digest)
    string(APPEND _id "${_digest}:")
  endforeach()
  set(${out} "${_id}" PARENT_SCOPE)
endfunction()
