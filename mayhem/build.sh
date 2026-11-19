#!/usr/bin/env bash
#
# mayhem/build.sh — build kakoune's fuzz harnesses and its test suite.
#
# Runs inside the commit image as `mayhem` in /mayhem. Uses upstream's own Makefile twice, with
# distinct object tags so the two trees never collide and a re-run is incremental:
#   1) fuzz build:  src/*.opt.fuzz.o  — $SANITIZER_FLAGS $DEBUG_FLAGS + SanCov (fuzzer-no-link)
#   2) test build:  src/*.opt.o       — upstream's normal flags; src/kak -> kak.opt for test/run
# (`sanitize=mayhem tag-sanitize-mayhem=.fuzz` only selects the object tag: the Makefile has no
# CXXFLAGS-sanitize-mayhem, so it adds no flags of its own.)
# Kakoune has no library: each harness compiles src/main.cc into itself (main renamed to
# kak_main) and links every other upstream object.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"
mkdir -p "$OUT"

# SanCov on the project too, so Mayhem/libFuzzer see coverage inside kakoune, not just the harness.
COV_FLAGS="-fsanitize=fuzzer-no-link"
# -O1 after upstream's -O3: faster sanitized build, clearer backtraces. $DEBUG_FLAGS last so
# -gdwarf-3 wins over upstream's -g3 (DWARF <= 3).
FUZZ_CXXFLAGS="$SANITIZER_FLAGS $COV_FLAGS -O1 $DEBUG_FLAGS"

# 1) Sanitized + instrumented upstream objects (and a sanitized src/kak.opt.fuzz as a by-product).
make -j"$MAYHEM_JOBS" CXX="$CXX" sanitize=mayhem tag-sanitize-mayhem=.fuzz \
     CXXFLAGS="$FUZZ_CXXFLAGS"

# Every upstream object except main.o: each harness compiles src/main.cc itself (see
# mayhem/harnesses/kak_fuzz_common.hh), to reach run_filter/run_server in its anonymous namespace.
objs=()
for o in src/*.opt.fuzz.o src/.version.opt.fuzz.o; do
  [ "$o" = src/main.opt.fuzz.o ] && continue
  objs+=("$o")
done

$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OUT/lsan_off.o"

HARNESS_CXXFLAGS="-std=c++2b -fsized-deallocation -Isrc -Imayhem/harnesses $SANITIZER_FLAGS $COV_FLAGS -O1 $DEBUG_FLAGS"
for h in kak_keys kak_session kak_script kak_regex; do
  $CXX $HARNESS_CXXFLAGS -c "mayhem/harnesses/$h.cc" -o "$OUT/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/$h.o" "$OUT/lsan_off.o" \
       "${objs[@]}" -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/$h.o" "$OUT/standalone_main.o" "$OUT/lsan_off.o" \
       "${objs[@]}" -o "$SRC/$h-standalone"
done

# 2) Test build with upstream's normal flags (last, so the src/kak symlink test/run uses points
#    at the unsanitized kak.opt).
make -j"$MAYHEM_JOBS" CXX="$CXX" CXXFLAGS="$COVERAGE_FLAGS"
ls -l src/kak src/kak.opt "$SRC"/kak_*
