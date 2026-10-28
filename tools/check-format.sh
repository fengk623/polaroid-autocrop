#!/usr/bin/env bash
# Fails if any C/C++ source differs from .clang-format (CI gate). Usage: tools/check-format.sh
set -euo pipefail
cd "$(dirname "$0")/.."
files=$(git ls-files 'core/*.cpp' 'core/*.hpp' 'core/*.h' 'app/*.cpp' 'app/*.hpp' 'tools/*.cpp' 'tools/*.hpp' 'tests/*.cpp' 'tests/*.hpp' 'platform/*.cpp' 'platform/*.hpp')
[ -z "$files" ] && exit 0
clang-format --dry-run --Werror $files
