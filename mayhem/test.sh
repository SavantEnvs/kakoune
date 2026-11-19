#!/usr/bin/env bash
#
# mayhem/test.sh — RUN kakoune's own functional suite (test/run, built by mayhem/build.sh).
#
# test/run drives src/kak through ~370 cases (test/<area>/<case>/{in,cmd,rc,script}) over the
# JSON UI and compares the edited buffer against the expected `out` (golden diffs) or the
# expected error text. Its last line is "Summary: <N> tests, <F> failures".
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

if [ ! -x src/kak ]; then
  echo "src/kak missing — mayhem/build.sh did not build the test binary" >&2
  emit_ctrf "kakoune-test-run" 0 1
  exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/kak-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
log="$work/log"
# test/run needs a UTF-8 locale and a writable runtime dir for the session socket.
env LC_ALL=C.UTF-8 XDG_RUNTIME_DIR="$work" TMPDIR="$work" HOME="$work" test/run >"$log" 2>&1
rc=$?
tail -n 30 "$log"

summary=$(sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -E '^Summary: [0-9]+ tests, [0-9]+ failures' | tail -n1)
if [ -z "$summary" ]; then
  echo "test/run produced no summary (exit $rc)" >&2
  emit_ctrf "kakoune-test-run" 0 1
  exit 1
fi
total=$(echo "$summary" | sed -E 's/^Summary: ([0-9]+) tests.*/\1/')
failed=$(echo "$summary" | sed -E 's/.* ([0-9]+) failures.*/\1/')
# a run that executed nothing is a failure, not a pass
if [ "$total" -eq 0 ]; then failed=1; fi
passed=$(( total - failed )); [ "$passed" -lt 0 ] && passed=0
emit_ctrf "kakoune-test-run" "$passed" "$failed"
