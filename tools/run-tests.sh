#!/usr/bin/env bash
# Full (or labelled) test run with an explicit executed / skipped / failed / duration report.
# Fails when any test is skipped unless its name is listed in tests/allowed-skips.txt (TEST-PLAN §2, trap T1).
#   tools/run-tests.sh <build-dir> [ctest args...]     e.g. tools/run-tests.sh build/dev -L geometry
set -uo pipefail
BUILD_DIR=${1:?usage: run-tests.sh <build-dir> [ctest args]}
shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
JUNIT="$BUILD_DIR/test-report.xml"
start=$(date +%s)
ctest --test-dir "$BUILD_DIR" --output-on-failure --output-junit "$(cd "$BUILD_DIR" && pwd)/test-report.xml" "$@"
status=$?
end=$(date +%s)
python3 "$ROOT/tools/test_report.py" "$JUNIT" "$ROOT/tests/allowed-skips.txt" "$((end - start))"
rstatus=$?
if [ $status -ne 0 ]; then exit $status; fi
exit $rstatus
