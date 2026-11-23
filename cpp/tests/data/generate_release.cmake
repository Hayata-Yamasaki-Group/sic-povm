# SPDX-License-Identifier: AGPL-3.0-or-later
include("${CMAKE_CURRENT_LIST_DIR}/generate.cmake")
file(REMOVE_RECURSE "${WORK}")
sic_generate_d2("${BIN}" "${WORK}")
sic_generate_layout("${BIN}" "${WORK}/data" "${WORK}/d2.sicx" 1 2)
sic_run("${BIN}/sic-inspect" --catalogue "${WORK}/data")
sic_generate_wh("${BIN}" "${WORK}/d4" 2 standard)
sic_generate_wh("${BIN}" "${WORK}/d8" 3 standard)
sic_generate_wh("${BIN}" "${WORK}/d8-maximal" 3 maximal)
