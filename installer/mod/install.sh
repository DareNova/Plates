#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
exec python3 installer.py "${1:-enable}" "${@:2}"
