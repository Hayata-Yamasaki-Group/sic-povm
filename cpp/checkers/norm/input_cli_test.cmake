# SPDX-License-Identifier: AGPL-3.0-or-later
# Small independent fixture only: extracting its footer establishes the test
# trust anchor. Real callers must obtain roots from an independent source.
file(READ "${FIXTURE}" _hex HEX)
string(LENGTH "${_hex}" _length)
math(EXPR _exact "${_length} - 224 + 64")
math(EXPR _file "${_length} - 224 + 128")
string(SUBSTRING "${_hex}" ${_exact} 64 _exact_root)
string(SUBSTRING "${_hex}" ${_file} 64 _file_root)
file(SHA256 "${FIXTURE}" _sha)
function(check_case name expected pattern)
  execute_process(COMMAND "${CHECKER}" "${FIXTURE}" ${ARGN}
    RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _status EQUAL expected OR NOT "${_out}${_err}" MATCHES "${pattern}")
    message(FATAL_ERROR "${name}: status ${_status}, expected ${expected}: ${_out}${_err}")
  endif()
endfunction()
if(DOMAIN_ONLY)
  check_case(general_input 0 "FULL SICX input verification: PASS" --verify-input)
  check_case(certificate_domain 3 "norm certificate supports power-of-two d only")
  return()
endif()
check_case(full 0 "FULL SICX input verification: PASS" --verify-input)
check_case(trusted 0 "trusted whole-file SHA-256 matched" --verify-input
  --exact-root "${_exact_root}" --file-root "${_file_root}" --sha256 "${_sha}")
check_case(partial 0 "PARTIAL SICX input verification: PASS" --partial-element 2 --exact-root "${_exact_root}")
check_case(partial_scope 0 "branch canonicality unchecked" --partial --exact-root "${_exact_root}")
check_case(partial_ball 0 "trusted identity matched" --partial-chunk 8
  --exact-root "${_exact_root}" --file-root "${_file_root}")
check_case(missing_exact_trust 3 "REJECTED" --partial-element 2)
check_case(missing_file_trust 3 "REJECTED" --partial-chunk 8 --exact-root "${_exact_root}")
check_case(wrong_exact 3 "expected exact root mismatch" --verify-input --exact-root "${_file_root}")
check_case(wrong_file 3 "expected file root mismatch" --verify-input --file-root "${_exact_root}")
check_case(partial_hash 0 "transitively trusted via whole-file SHA-256" --partial --sha256 "${_sha}")
check_case(partial_file_trust 0 "transitively trusted via file root and complete LDIR"
  --partial-element 2 --file-root "${_file_root}")
check_case(whole_hash_trust 0 "transitively trusted via whole-file SHA-256" --verify-input --sha256 "${_sha}")
check_case(unknown_option 2 "unknown argument" --unknown-option)
message(STATUS "PASS: 13 input CLI scenarios")
