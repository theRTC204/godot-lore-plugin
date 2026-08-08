#!/usr/bin/env bash
# Verifies that a godot-lore-plugin platform directory (staged build output,
# or an assembled release package's per-platform subfolder) contains the
# expected files: the plugin's own build output and Lore's native shared
# library. Filename-agnostic on both counts, since a single-arch macOS build
# isn't named the same as the universal release artifact, and the Lore
# library's filename differs per platform (lore.dll / liblore.so /
# liblore.dylib).
#
# Usage: verify-platform-package.sh <platform_dir>
set -euo pipefail

platform_dir="$1"

test -n "$(find "$platform_dir" -maxdepth 1 -iname 'godot-lore-plugin*' -print -quit)"
test -n "$(find "$platform_dir" -maxdepth 1 \( -iname 'lore.dll' -o -iname 'liblore.*' \) -print -quit)"
