#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# nightly.sh: the overnight tests, run by hand, e.g. in the evening:
#   ./nightly.sh &
# Runs on a snapshot of the last commit (git archive HEAD) in
# build/nightly/<date-time>/src, so changes during the run do not disturb it;
# uncommitted changes are not tested. Each job has a time limit.
# Results: build/nightly/<date-time>/summary.txt, one line per job:
# PASS, FAIL or TIMEOUT, seconds, log. build/nightly/latest points to the last
# run. sby work directories with counterexamples stay in src/formal.
#
# U8G2: a u8g2 tree with the clut branch, default ../u8g2.

set -u
PATH="$PATH:/opt/oss-cad-suite/bin"
export PATH

repo=$(git rev-parse --show-toplevel) || exit 1
cd "$repo" || exit 1
u8g2=$(cd "${U8G2:-$repo/../u8g2}" && pwd) || exit 1

stamp=$(date +%Y%m%d-%H%M)
out="$repo/build/nightly/$stamp"
src="$out/src"
summary="$out/summary.txt"
mkdir -p "$src"
git archive HEAD | tar -x -C "$src"
ln -sfn "$stamp" "$repo/build/nightly/latest"

{
  echo "nightly $stamp"
  echo "commit $(git rev-parse --short HEAD) $(git log -1 --format=%s)"
  if [ -n "$(git status --porcelain)" ]; then
    echo "note: uncommitted changes, not tested"
  fi
  echo
} > "$summary"

# job NAME LIMIT COMMAND: run COMMAND in src with a time limit, log in NAME.log
job() {
  name=$1 limit=$2
  shift 2
  start=$(date +%s)
  (cd "$src" && timeout "$limit" sh -c "$*") > "$out/$name.log" 2>&1
  rc=$?
  case $rc in
    0)   result=PASS ;;
    124) result=TIMEOUT ;;
    *)   result=FAIL ;;
  esac
  printf '%-16s %-8s %7ss  %s\n' "$name" "$result" $(( $(date +%s) - start )) "$name.log" >> "$summary"
}

# simulation
job test         30m "make test U8G2=$u8g2"
job test-nocache 30m "make test-nocache U8G2=$u8g2"
job fabric       1h  "make fabric U8G2=$u8g2"
job measure      4h  "make -s measure U8G2=$u8g2 WAIT_STATES='0 1 2 4 8 16 24 32' > measure.out 2>&1; cat measure.out; ! grep -q '^output differs\|, [1-9][0-9]* errors' measure.out"

# formal: the short tasks, then the long ones
job formal       1h  "make formal"
job fbmem-big    4h  "cd formal && sby -f fbmem.sby prove_big cover_big"

echo "done $(date +%Y%m%d-%H%M)" >> "$summary"
cat "$summary"
