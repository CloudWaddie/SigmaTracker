#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
python3 tools/build_dictionary.py --seed
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I main main/core.cpp tests/core_test.cpp -o "$d/steno_test"
"$d/steno_test"
python3 -m py_compile tools/build_dictionary.py tools/serial_bridge.py
if [[ "${1:-}" == "--full" ]]; then
    python3 tools/build_dictionary.py
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -I main main/core.cpp tests/dictionary_test.cpp -o "$d/dictionary_test"
    "$d/dictionary_test"
fi
