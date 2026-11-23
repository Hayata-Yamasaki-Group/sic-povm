# SPDX-License-Identifier: AGPL-3.0-or-later
# Closed-form tetrahedral ray and its conjugate, plus a simple qutrit SIC.
# Keep the decimal encoding explicit so the inputs need only CMake.
set(SIC_TEST_NUMERIC_DIR "${PROJECT_BINARY_DIR}/test-vectors")
foreach(case d2 d2-conjugate d3)
  file(MAKE_DIRECTORY "${SIC_TEST_NUMERIC_DIR}/${case}")
endforeach()
file(WRITE "${SIC_TEST_NUMERIC_DIR}/d2/fiducial-fp64.csv"
  "index,real,imag\n0,0.8880738339771153,0\n1,0.3250575836718682,0.3250575836718682\n")
file(WRITE "${SIC_TEST_NUMERIC_DIR}/d2-conjugate/fiducial-fp64.csv"
  "index,real,imag\n0,0.8880738339771153,0\n1,0.3250575836718682,-0.3250575836718682\n")
file(WRITE "${SIC_TEST_NUMERIC_DIR}/d3/fiducial-fp64.csv"
  "index,real,imag\n0,0,0\n1,1,0\n2,-1,0\n")
file(WRITE "${SIC_TEST_NUMERIC_DIR}/catalogue.tsv"
  "2 synthetic-d2 - test d2/fiducial-fp64.csv\n")
