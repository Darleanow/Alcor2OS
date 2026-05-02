#!/bin/sh
# macos-disk-preserve.sh <image> <staging-dir>
#
# Extract /home and /tmp from the prior ext2 image into the staging dir so
# they survive a `mke2fs -d` disk rebuild on macOS. Best-effort: a silent
# no-op when the image doesn't yet exist or e2tools isn't installed.

IMAGE="$1"
STAGING="$2"

if [ -z "$IMAGE" ] || [ -z "$STAGING" ]; then
    echo "usage: $0 <image> <staging-dir>" >&2
    exit 1
fi

[ -f "$IMAGE" ] || exit 0

if ! command -v e2cp >/dev/null 2>&1; then
    echo "[disk] e2tools not installed; /home and /tmp will reset on each build"
    echo "[disk]   brew install e2tools  to enable persistence"
    exit 0
fi

walk() {
    src=$1
    dst=$2
    e2ls "$IMAGE:$src" >/dev/null 2>&1 || return 0
    mkdir -p "$dst" 2>/dev/null
    e2ls "$IMAGE:$src" 2>/dev/null | tr ' ' '\n' | while IFS= read -r entry; do
        [ -z "$entry" ] && continue
        case "$entry" in '.'|'..') continue ;; esac
        if e2cp "$IMAGE:$src/$entry" "$dst/$entry" 2>/dev/null; then
            :
        else
            walk "$src/$entry" "$dst/$entry"
        fi
    done
}

walk /home "$STAGING/home"
walk /tmp  "$STAGING/tmp"
echo "[disk] preserved /home and /tmp from $IMAGE"
exit 0
