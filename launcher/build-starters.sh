#!/usr/bin/env bash
# Builds the starters TLOU Launcher.exe (tlou-launcher.c) and TLOU Game.exe (tlou-game.c): stripped,
# without a C runtime, with version information and a manifest (tlou-starter.rc). Small unsigned
# programs that start a hidden interpreter look like malware loaders to antivirus heuristics
# (issues #88, #112, #148). Needs clang and windres (MSYS2 CLANG64).
# usage: build-starters.sh <TLOU Launcher.exe path> <TLOU Game.exe path> [--icon]
#   --icon  gives TLOU Launcher.exe launcher/assets/icon.ico when it is there (not in the repository)
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
launcher_out=$1
game_out=$2
icon=${3:-}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
# windres preprocesses through a temporary file. MSYS2's bash started from Git Bash has no
# TMP/TEMP, and without them it stopped with "Unable to create temp file".
TMP=$(cygpath -w "$work")
TEMP=$TMP
export TMP TEMP

# No C runtime (tlou-starter-crt.h): kernel32 and user32 only, no TLS callbacks or
# VirtualProtect from the MinGW start-up code. No time stamp: the same sources and compiler give
# the same bytes, so the files keep their hash (and any antivirus vendor's verdict) between
# releases.
build() {  # <source> <output> <windres options...>
    local source=$1 output=$2
    shift 2
    windres -I "$here" "$@" "$here/tlou-starter.rc" -O coff -o "$work/resources.o"
    clang -O2 -s -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -nostdlib \
        -Wl,--subsystem,windows -Wl,-e,starter_entry -Wl,--no-insert-timestamp \
        "$here/$source" "$work/resources.o" -lkernel32 -luser32 -o "$output"
}

launcher_options=(-DTLOU_STARTER_LAUNCHER)
if [[ $icon == --icon && -f $here/assets/icon.ico ]]; then
    launcher_options+=(-DTLOU_STARTER_ICON)
fi
build tlou-launcher.c "$launcher_out" "${launcher_options[@]}"
build tlou-game.c "$game_out" -DTLOU_STARTER_GAME
