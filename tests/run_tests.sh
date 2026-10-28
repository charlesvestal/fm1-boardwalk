#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Boardwalk's tests: the ringing core (tests/host/ringing_test.c), the sound engine (tests/host/bw_test.c: writes
# app/build/tests/rounds.wav), the whole app in fm1-base's simulator (tests/scenarios/*.bw), the browser build, the
# screen reader's words (fm1-reader's audit), and the update path against Boardwalk's package (./build.sh first). Screenshots and audio in app/build/scenarios/. fm1-base's own
# tests: base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files, as ./build.sh
B=app/build
OUT=$B/tests
FAIL=0
mkdir -p "$OUT" $B/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
W="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror"
run ringing sh -c "$CC $W -Iapp/games -o $OUT/ringing_test tests/host/ringing_test.c app/games/ringing.c && $OUT/ringing_test"
run bw sh -c "$CC $W -DBW_HOST -Iapp/dsp -Iapp/games -Ibase/include -o $OUT/bw_test tests/host/bw_test.c app/dsp/bw.c \
    app/games/ringing.c -lm && $OUT/bw_test $OUT"
if [ -f $B/boardwalk.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/boardwalk.fwsc"
    run update-loader sh -c "head -c 60000 $B/boardwalk.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/boardwalk.fwsc"
else
    echo "  skip update path (no $B/boardwalk.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    run emu sh -c "sh build.sh --web && node tests/host/emu_test.mjs $B/emu/app.wasm"
else
    echo "  skip emu (no emcc)"
fi
for s in tests/scenarios/*.bw; do
    n=$(basename "$s" .bw)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/boardwalk_host "$s" "$B/scenarios/$n"
done
# the screen reader's words (fm1-reader's audit, beside this repo): every screen in the dump, every message the
# scenarios sent
if command -v node >/dev/null 2>&1 && [ -f ../fm1-reader/tools/audit.mjs ]; then
    SR=$B/sr
    mkdir -p $SR
    printf 'wait 500\nspeakdump\n' > $SR/dump.bw
    run reader-audit sh -c "$B/host/boardwalk_host $SR/dump.bw $SR | grep -v '^host' | sort -u > $SR/dump.tsv && \
        rm -f $SR/said.tsv && for f in tests/scenarios/*.bw; do n=\$(basename \$f .bw); \
        FM1B_SAID_LOG=\$PWD/$SR/said.tsv $B/host/boardwalk_host \$f $B/scenarios/\$n >/dev/null; done; \
        sort -u $SR/said.tsv | cat $SR/dump.tsv - > $SR/all.tsv && \
        node ../fm1-reader/tools/audit.mjs $SR/all.tsv --words app/web/words/\$(grep -o '[0-9a-f]*\.json' $B/gen/ann_words.h) \
        --allow app/words.allow | tee /dev/stderr | grep -q ' 0 errors, 0 warnings'"
else
    echo "  skip reader-audit (no node, or no ../fm1-reader)"
fi
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
