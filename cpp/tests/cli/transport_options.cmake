# SPDX-License-Identifier: AGPL-3.0-or-later
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
# Both callbacks must start with the default SIGPIPE disposition. An inherited
# SIG_IGN would let the probe exit 42 instead of being terminated by SIGPIPE.
file(WRITE "${WORK}/check-sigpipe.sh" [=[
sh -c 'kill -s PIPE "$$"; exit 42' 2>/dev/null
[ "$?" -eq 141 ] || exit 43
exec "$@"
]=])
set(check_sigpipe "sh '${WORK}/check-sigpipe.sh'")
# The upload command only records the size of its local argument.
file(WRITE "${WORK}/upload.sh" "#!/bin/sh\nstat -c %s \"$1\" > '${WORK}/confirmed-size'\n")
set(upload "${check_sigpipe} sh '${WORK}/upload.sh'")
set(confirm "${check_sigpipe} cat '${WORK}/confirmed-size'")
execute_process(COMMAND "${BIN}/sic-container" split "${FIXTURE}" --out "${WORK}/assets"
  --asset-prefix d4-m1 --repository example/project --tag test-release
  --emit-and-upload "${upload}" --confirm-size-cmd "${confirm}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(parts "${WORK}/assets/multiplet.sicx.parts.json")
if(NOT rc EQUAL 0 OR NOT EXISTS "${parts}" OR NOT EXISTS "${WORK}/confirmed-size" OR NOT err MATCHES "uploaded ")
  message(FATAL_ERROR "local upload: ${rc} ${out} ${err}")
endif()
file(READ "${parts}" json)
string(JSON repository GET "${json}" release repository)
string(JSON tag GET "${json}" release tag)
string(JSON part_size GET "${json}" parts 0 size_bytes)
string(JSON part_sha GET "${json}" parts 0 sha256)
string(JSON asset GET "${json}" parts 0 asset)
file(SIZE "${FIXTURE}" expected_size)
file(SHA256 "${FIXTURE}" expected_sha)
file(READ "${WORK}/confirmed-size" confirmed_size)
string(STRIP "${confirmed_size}" confirmed_size)
if(NOT part_size STREQUAL "${expected_size}" OR NOT confirmed_size STREQUAL "${expected_size}" OR
    NOT part_sha STREQUAL expected_sha OR EXISTS "${WORK}/assets/${asset}")
  message(FATAL_ERROR "confirmed part does not match fixture or remains staged")
endif()
if(NOT repository STREQUAL "example/project" OR NOT tag STREQUAL "test-release")
  message(FATAL_ERROR "release override was not recorded")
endif()
foreach(failure failed_upload size missing_confirmation confirmation_only)
  set(args --emit-and-upload "${upload}" --confirm-size-cmd "${confirm}")
  if(failure STREQUAL failed_upload)
    set(args --emit-and-upload false --confirm-size-cmd "${confirm}")
  elseif(failure STREQUAL size)
    set(args --emit-and-upload "${upload}" --confirm-size-cmd "printf 0")
  elseif(failure STREQUAL missing_confirmation)
    set(args --emit-and-upload "${upload}")
  else()
    set(args --confirm-size-cmd "${confirm}")
  endif()
  execute_process(COMMAND "${BIN}/sic-container" split "${FIXTURE}" --out "${WORK}/${failure}"
    --asset-prefix d4-m1 --repository example/project --tag test-release ${args}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(failure MATCHES "confirmation")
    set(expected_rc 2)
    set(kind invalid_argument)
  else()
    set(expected_rc 1)
    if(failure STREQUAL failed_upload)
      set(kind io)
    else()
      set(kind integrity)
    endif()
  endif()
  if(NOT rc EQUAL expected_rc OR NOT err MATCHES "\\[${kind}\\]" OR EXISTS "${WORK}/${failure}/multiplet.sicx.parts.json")
    message(FATAL_ERROR "${failure} refusal: ${rc} ${out} ${err}")
  endif()
endforeach()
