#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

GAME_DIR="${TLOU_GAME_DIR:-$ROOT/../CUSA00559}"
if [[ $# -gt 0 && -d "$1" ]]; then
    GAME_DIR="$1"
    shift
fi

if [[ ! -x "$ROOT/gow3_pc/out/gow3-probe" ]]; then
    echo "Linux runtime not found: gow3_pc/out/gow3-probe" >&2
    echo "Download the Linux build or build it with: bash gow3_pc/build.sh" >&2
    exit 1
fi

if [[ ! -f "$GAME_DIR/eboot.bin" ]]; then
    echo "The game folder does not contain eboot.bin: $GAME_DIR" >&2
    echo "Usage: bash Start-TLOU-Linux.sh [/path/to/CUSA00559]" >&2
    echo "Or set TLOU_GAME_DIR=/path/to/CUSA00559." >&2
    exit 2
fi

# The Linux package carries SDL3 next to the runtime. Other system libraries (Vulkan,
# FFmpeg, X11/Wayland) are provided by the host distribution.
export LD_LIBRARY_PATH="$ROOT/gow3_pc/out${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$ROOT/gow3_pc/out/pydeps${PYTHONPATH:+:$PYTHONPATH}"

exec python3 "$ROOT/tools/probe_tlou.py" --game "$GAME_DIR" --boot --seconds 0 "$@"
