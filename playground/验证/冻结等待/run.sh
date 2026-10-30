#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$test_dir/../../.." && pwd)"
build_mode="${1:-debug}"
case "$build_mode" in
    debug|release) ;;
    *) echo "usage: $0 [debug|release]" >&2; exit 1 ;;
esac

"$project_dir/scripts/build.sh" playground-build "$test_dir" "$build_mode"
program="$project_dir/build-$build_mode/bin/domino-playground"
test_sandbox="$(mktemp -d "${TMPDIR:-/tmp}/domino-freeze-test.XXXXXX")"
trap 'rm -rf -- "$test_sandbox"' EXIT

timeout 60s "$program" time
timeout 60s "$program" lifecycle "$test_sandbox/world" > "$test_sandbox/lifecycle.log" 2>&1 || {
    cat "$test_sandbox/lifecycle.log"
    exit 1
}
cat "$test_sandbox/lifecycle.log"
awk '
    /FREEZE_LOG_MARKER_/ { logged++ }
    /STOP_RETURNED_/ { stopped++; if (logged != stopped) failed = 1 }
    END { exit (failed || stopped != 2 || logged != 2) }
' "$test_sandbox/lifecycle.log"

timeout 60s "$program" stop-races "$test_sandbox/stop-races-world" > "$test_sandbox/stop-races.log" 2>&1 || {
    cat "$test_sandbox/stop-races.log"
    exit 1
}
tail -n 1 "$test_sandbox/stop-races.log"

for case_name in start-before-init freeze-before-start destroy-before-stop wrong-thread-stop duplicate-stop thaw-after-stop thaw-while-running; do
    exit_status=0
    timeout 15s "$program" invalid "$case_name" "$test_sandbox/$case_name" > "$test_sandbox/$case_name.log" 2>&1 || exit_status=$?
    if [[ "$exit_status" != 1 ]]; then
        cat "$test_sandbox/$case_name.log"
        echo "invalid lifecycle case $case_name returned $exit_status, expected EXIT_FAILURE (1)" >&2
        exit 1
    fi
    case "$case_name" in
        start-before-init) expected_message="start requires an initialized engine" ;;
        freeze-before-start) expected_message="freeze requires the running engine" ;;
        destroy-before-stop) expected_message="destroy requires a stopped engine" ;;
        wrong-thread-stop|duplicate-stop) expected_message="stop requires an initialized or running engine" ;;
        thaw-after-stop) expected_message="thaw requires the running engine" ;;
        thaw-while-running) expected_message="解冻时间失败: 时钟未冻结" ;;
    esac
    if ! rg -Fq -- "$expected_message" "$test_sandbox/$case_name.log"; then
        cat "$test_sandbox/$case_name.log"
        echo "invalid lifecycle case $case_name failed for an unexpected reason" >&2
        exit 1
    fi
done
echo "freeze tests passed: $build_mode; time waits, lifecycle, 128 Stop races, log drain, and 7 invalid-call cases"
