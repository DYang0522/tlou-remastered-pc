#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if ! python3 -c 'import tkinter' >/dev/null 2>&1; then
    echo "Tkinter is missing. Install it with: sudo apt install python3-tk" >&2
    exit 1
fi

export LD_LIBRARY_PATH="$ROOT/gow3_pc/out${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$ROOT/gow3_pc/out/pydeps${PYTHONPATH:+:$PYTHONPATH}"

exec python3 "$ROOT/launcher/tlou_launcher.pyw" "$@"
