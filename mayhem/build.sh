#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Apollo's real build is Bazel inside its multi-GB dev container. The fuzzed code here (the UDP
# bridge header codec and the vendored RTKLIB decoders) has no dependencies, so it is compiled
# directly from the upstream sources with the fuzz flags. Everything lands in mayhem/out/
# (gitignored), so a re-run or a `git clean -ffdX`-ed tree rebuilds from scratch.
OUT="$SRC/mayhem/out"
OBJ="$OUT/obj"
mkdir -p "$OBJ/fuzz" "$OBJ/sa" "$OBJ/test"

FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1"
# The libFuzzer objects also carry SanitizerCoverage; the standalone reproducers are built from a
# second, coverage-free object set so they link with no fuzzer runtime (even with SANITIZER_FLAGS=).
COV_FLAGS="-fsanitize=fuzzer-no-link"
# BridgeHeader::Diserialize reads HType/bsize through reinterpret_cast at the odd offset the wire
# format puts them (25 bytes after the flag), so UBSan's alignment check fires on every datagram
# (benign on x86), and it loads the wire's item type straight into `enum HType` before its own
# range check (`type >= Header_Tail || type < 0`), which UBSan's enum check flags for every unknown
# item type. Only those two checks are relaxed, only for the TUs that instantiate the bridge
# codec (bridge_header.cc and the harness, which inlines bridge_header_item.h).
BRIDGE_UB="-fno-sanitize=alignment,enum"
INC="-I$SRC"
# bridge_header_item.h uses uint32_t without including <cstdint>.
CXXINC="$INC -include cstdint"
RTKLIB_SRCS="novatel rcvraw rtcm rtcm3 rtkcmn"

# 1) The project code, sanitized + DWARF 3, once per object set (fuzz = +coverage, sa = standalone).
for set in fuzz sa; do
  cov=""; [ "$set" = fuzz ] && cov="$COV_FLAGS"
  for f in $RTKLIB_SRCS; do
    $CC $FUZZ_FLAGS $cov -w $INC -c "third_party/rtklib/$f.c" -o "$OBJ/$set/rtklib_$f.o"
  done
  $CXX $FUZZ_FLAGS $BRIDGE_UB $cov $CXXINC -c modules/bridge/common/bridge_header.cc -o "$OBJ/$set/bridge_header.o"
  # 2) Harnesses.
  $CXX $FUZZ_FLAGS $BRIDGE_UB $cov $CXXINC -c mayhem/harnesses/fuzz_bridge_header.cc -o "$OBJ/$set/fuzz_bridge_header.o"
  $CC $FUZZ_FLAGS $cov $INC -c mayhem/harnesses/fuzz_rtklib_rtcm3.c -o "$OBJ/$set/fuzz_rtklib_rtcm3.o"
  $CC $FUZZ_FLAGS $cov $INC -c mayhem/harnesses/fuzz_rtklib_oem4.c -o "$OBJ/$set/fuzz_rtklib_oem4.o"
done
$CXX $FUZZ_FLAGS -c mayhem/lsan_off.cc -o "$OBJ/sa/lsan_off.o"
$CC $FUZZ_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/sa/standalone_main.o"

# Each harness: a libFuzzer binary + a run-once standalone reproducer (<name>-standalone).
link() {  # link <name> <objs relative to the object set...>
  local name="$1"; shift
  local o fz=() sa=()
  for o in "$@"; do fz+=("$OBJ/fuzz/$o"); sa+=("$OBJ/sa/$o"); done
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "${fz[@]}" "$OBJ/sa/lsan_off.o" -lm -lpthread -o "$OUT/$name"
  $CXX $FUZZ_FLAGS "$OBJ/sa/standalone_main.o" "${sa[@]}" "$OBJ/sa/lsan_off.o" -lm -lpthread \
    -o "$OUT/$name-standalone"
}
RTKLIB_OBJS=$(for f in $RTKLIB_SRCS; do printf 'rtklib_%s.o ' "$f"; done)
link fuzz_bridge_header fuzz_bridge_header.o bridge_header.o
link fuzz_rtklib_rtcm3 fuzz_rtklib_rtcm3.o $RTKLIB_OBJS
link fuzz_rtklib_oem4 fuzz_rtklib_oem4.o $RTKLIB_OBJS

# 1b) OpenDRIVE map adapter, Cyber RT record reader, Velodyne calibration. These need
#     protobuf/glog/tinyxml2/proj/yaml-cpp
#     (apt, installed by mayhem/Dockerfile) and the protoc-generated message classes, which are
#     generated here from the committed .proto files into mayhem/out/gen.
GEN="$OUT/gen"
mkdir -p "$GEN"
PROTOS="cyber/proto/record.proto"
todo="modules/common_msgs/map_msgs/map.proto modules/common_msgs/basic_msgs/pnc_point.proto modules/common_msgs/basic_msgs/error_code.proto
  modules/drivers/gnss/proto/config.proto $(ls modules/common_msgs/sensor_msgs/{gnss,gnss_best_pose,gnss_raw_observation,heading,imu,ins}.proto | tr '\n' ' ')"
seen=" "
while [ -n "$todo" ]; do   # import closure of the map protos
  set -- $todo; f="$1"; shift; todo="$*"
  case "$seen" in *" $f "*) continue ;; esac
  seen="$seen$f "
  todo="$todo $(sed -n 's/^import "\(.*\)";/\1/p' "$f" | tr '\n' ' ')"
done
PROTOS="$PROTOS $(echo $seen | tr ' ' '\n' | sort -u | tr '\n' ' ')"
protoc -I"$SRC" --cpp_out="$GEN" $PROTOS
PB_SRCS=$(for p in $PROTOS; do printf '%s ' "$GEN/${p%.proto}.pb.cc"; done)
MAP_SRCS="modules/map/hdmap/adapter/opendrive_adapter.cc modules/map/hdmap/adapter/proto_organizer.cc
  $(ls modules/map/hdmap/adapter/xml_parser/*.cc | grep -v _test.cc | tr '\n' ' ')
  modules/common/math/vec2d.cc modules/common/math/polygon2d.cc modules/common/math/line_segment2d.cc
  modules/common/math/box2d.cc modules/common/math/aabox2d.cc modules/common/math/math_utils.cc
  modules/common/util/string_util.cc modules/common/configs/config_gflags.cc"
COMMON_SRCS="cyber/binary.cc $PB_SRCS"
REC_SRCS="cyber/record/record_reader.cc cyber/record/file/record_file_reader.cc
  cyber/record/file/record_file_base.cc cyber/record/file/record_file_writer.cc
  cyber/record/header_builder.cc cyber/common/file.cc cyber/time/time.cc cyber/time/duration.cc"
CAL_SRCS="modules/drivers/lidar/velodyne/parser/calibration.cc"
# The NovAtel log parser includes cyber/cyber.h; harnesses/compat/cyber/cyber.h stands in for it
# (log macros, cyber::Time and cyber::OK() only), so the parser builds without the Cyber RT framework.
# NovatelParser reads the log header and the trailing CRC through reinterpret_cast at whatever
# offset the frame puts them (check_crc: `*reinterpret_cast<uint32_t*>(buffer_.data() + l)`), so
# UBSan's alignment check fires on every log whose length is not a multiple of 4 (benign on x86).
# Only that check is relaxed, only for the harness TU, which includes novatel_parser.cc.
NOVATEL_UB="-fno-sanitize=alignment"
# The harness compiles novatel_parser.cc into its own TU (see the harness for why).
GNSS_SRCS="cyber/time/time.cc cyber/time/duration.cc"
# harnesses/compat/proj_api.h: the PROJ 4 API the coordinate converter includes, on PROJ 9.
DEP_INC="-I$SRC/mayhem/harnesses/compat -I$SRC -I$GEN -I/usr/include/eigen3"
DEP_LIBS="-lyaml-cpp -lprotobuf -lglog -lgflags -ltinyxml2 -lproj -labsl_strings -labsl_strings_internal
  -labsl_int128 -labsl_raw_logging_internal -labsl_throw_delegate -lpthread -lm"
objname() { echo "$1" | sed "s|^$GEN/||; s|^$SRC/||; s|[/.]|_|g"; }
compile_set() {  # compile_set <set> <flags> <srcs...>  (parallel, -j MAYHEM_JOBS)
  local set="$1" flags="$2"; shift 2
  local s
  for s in "$@"; do printf '%s\0%s\0' "$s" "$OBJ/$set/$(objname "$s").o"; done |
    xargs -0 -n2 -P"$MAYHEM_JOBS" sh -c '$0 -std=c++17 '"$flags $DEP_INC"' -c "$1" -o "$2"' "$CXX"
}
objs_for() { local set="$1" s; shift; for s in "$@"; do printf '%s ' "$OBJ/$set/$(objname "$s").o"; done; }
for set in fuzz sa; do
  cov=""; [ "$set" = fuzz ] && cov="$COV_FLAGS"
  compile_set "$set" "$FUZZ_FLAGS $cov" $MAP_SRCS $REC_SRCS $CAL_SRCS $COMMON_SRCS \
    mayhem/harnesses/fuzz_opendrive.cc mayhem/harnesses/fuzz_record_reader.cc \
    mayhem/harnesses/fuzz_velodyne_calibration.cc
  compile_set "$set" "$FUZZ_FLAGS $NOVATEL_UB $cov" mayhem/harnesses/fuzz_novatel_parser.cc
done
link_dep() {  # link_dep <name> <srcs...> [-- <prebuilt objs in the object set...>]
  local name="$1"; shift
  local srcs=() pre=() o
  while [ $# -gt 0 ] && [ "$1" != -- ]; do srcs+=("$1"); shift; done
  [ $# -gt 0 ] && shift
  pre=("$@")
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE $(objs_for fuzz "${srcs[@]}") \
    $(for o in "${pre[@]}"; do printf '%s ' "$OBJ/fuzz/$o"; done) "$OBJ/sa/lsan_off.o" $DEP_LIBS -o "$OUT/$name"
  $CXX $FUZZ_FLAGS "$OBJ/sa/standalone_main.o" $(objs_for sa "${srcs[@]}") \
    $(for o in "${pre[@]}"; do printf '%s ' "$OBJ/sa/$o"; done) "$OBJ/sa/lsan_off.o" $DEP_LIBS \
    -o "$OUT/$name-standalone"
}
link_dep fuzz_opendrive mayhem/harnesses/fuzz_opendrive.cc $MAP_SRCS $COMMON_SRCS
link_dep fuzz_record_reader mayhem/harnesses/fuzz_record_reader.cc $REC_SRCS $COMMON_SRCS
link_dep fuzz_velodyne_calibration mayhem/harnesses/fuzz_velodyne_calibration.cc $CAL_SRCS
link_dep fuzz_novatel_parser mayhem/harnesses/fuzz_novatel_parser.cc $GNSS_SRCS $COMMON_SRCS \
  -- $RTKLIB_OBJS

# 3) Functional known-answer suite, NORMAL flags (no sanitizers), for mayhem/test.sh.
for f in $RTKLIB_SRCS; do
  $CC -O1 -w $COVERAGE_FLAGS $INC -c "third_party/rtklib/$f.c" -o "$OBJ/test/rtklib_$f.o"
done
$CXX -O1 $COVERAGE_FLAGS $CXXINC -c modules/bridge/common/bridge_header.cc -o "$OBJ/test/bridge_header.o"
$CXX -O1 $COVERAGE_FLAGS $CXXINC mayhem/selftest/apollo_selftest.cc "$OBJ/test/bridge_header.o" \
  $(for f in $RTKLIB_SRCS; do printf '%s ' "$OBJ/test/rtklib_$f.o"; done) -lm -lpthread \
  -o "$OUT/apollo_selftest"
compile_set test "-O1 $COVERAGE_FLAGS" $MAP_SRCS $REC_SRCS $CAL_SRCS $COMMON_SRCS mayhem/selftest/map_record_selftest.cc
$CXX -O1 $COVERAGE_FLAGS $(objs_for test mayhem/selftest/map_record_selftest.cc $MAP_SRCS $REC_SRCS $CAL_SRCS $COMMON_SRCS) \
  $DEP_LIBS -o "$OUT/map_record_selftest"

echo "build.sh: built $(ls "$OUT" | grep -v obj | tr '\n' ' ')"
