#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# generate-goldens.sh BINARY WORK OUT.sha256 [REGEX]: run every case of cases.txt (or those whose name
# matches REGEX) with the native producer BINARY in a clean environment; append "<sha256>  <case>" to
# OUT.sha256 and "case NAME exit=.. wall=.. bytes=.." lines to OUT.provenance. Never overwrite a
# recorded golden file: record any intended fixture change explicitly.
set -u
bin=$(readlink -f "$1"); work=$2; out=$3; filter=${4:-.}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
prov="${out%.sha256}.provenance"
{ echo "binary $(sha256sum "$bin" | cut -c1-64) $bin"; echo "host $(hostname) $(date -u +%FT%TZ)";
  echo "cpus $(taskset -p $$ | awk '{print $NF}') $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"; } >> "$prov"
grep -v '^#' "$here/cases.txt" | while IFS='|' read -r name labels envs args; do
  [[ "$name" =~ $filter ]] || continue
  o="$work/$name.sicx"; rm -f "$o"*
  t0=$(date +%s.%N)
  controls=(); options=()
  for option in $envs; do
    if [[ $option == SIC_DEV_*=* ]]; then controls+=("$option"); else options+=("$option"); fi
  done
  env -i PATH=/usr/bin:/bin HOME="$HOME" SIC_THREADS="${SIC_THREADS:-1}" OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 "${controls[@]}" "$bin" $args "${options[@]}" --out "$o" > "$work/$name.log" 2>&1
  rc=$?; t1=$(date +%s.%N)
  if [ $rc -ne 0 ] || ! grep -q 'SIC check .*PASS' "$work/$name.log"; then
    echo "FAIL $name exit=$rc" >> "$prov"; continue
  fi
  echo "$(sha256sum < "$o" | cut -c1-64)  $name" >> "$out"
  echo "case $name exit=$rc wall=$(echo "$t1 - $t0" | bc) bytes=$(stat -c %s "$o")" >> "$prov"
  rm -f "$o"
done
