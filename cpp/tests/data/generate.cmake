# SPDX-License-Identifier: AGPL-3.0-or-later
function(sic_run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "${ARGN}: ${rc}\n${out}${err}")
  endif()
endfunction()

function(sic_generate_d2 bin work)
  file(MAKE_DIRECTORY "${work}")
  sic_run("${bin}/sic-construct" 1 --out "${work}/d2.sicx" --rays-out "${work}/rays.json" --threads 1)
  sic_run("${bin}/sic-decode" "${work}/d2.sicx" "${work}/decoded"
    --threads 1 --no-pin --finalize --ray-list "${work}/rays.json")
endfunction()

function(sic_produce_wh bin work exponent order threads)
  file(MAKE_DIRECTORY "${work}")
  sic_run("${bin}/sic-construct" "${exponent}" --order "${order}" --threads "${threads}"
    --out "${work}/multiplet.sicx" --rays-out "${work}/rays.json")
endfunction()

function(sic_finalize_wh bin work threads)
  sic_run("${bin}/sic-decode" "${work}/multiplet.sicx" "${work}/decoded"
    --threads "${threads}" --no-pin --finalize --ray-list "${work}/rays.json")
endfunction()

function(sic_generate_wh bin work exponent order)
  set(threads 1)
  if(ARGC GREATER 4)
    set(threads "${ARGV4}")
  endif()
  sic_produce_wh("${bin}" "${work}" "${exponent}" "${order}" "${threads}")
  file(SHA256 "${work}/multiplet.sicx" exact_sha256)
  file(WRITE "${work}/exact.sha256" "${exact_sha256}\n")
  if(ARGC GREATER 5)
    file(COPY_FILE "${work}/multiplet.sicx" "${ARGV5}")
  endif()
  sic_finalize_wh("${bin}" "${work}" "${threads}")
endfunction()

function(sic_generate_layout bin root input multiplet dimension)
  sic_run("${bin}/sic-data" layout "${input}" "${root}" --multiplet "${multiplet}" --threads 1)
  set(dir "${root}/d${dimension}/m${multiplet}")
  file(GLOB rays "${dir}/c*.json")
  foreach(ray IN LISTS rays)
    sic_run("${bin}/sic-data" write "${ray}" "${dir}" --formats csv)
  endforeach()
endfunction()
