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
    # `e2ls -l` lines look like: "<inode> <perms> <uid> <gid> <size> <date> <name>".
    # Find the 10-char perms field by pattern (first column position varies by version),
    # then the leading 'd' tells us it's a directory.
    e2ls -l "$IMAGE:$src" 2>/dev/null | awk '
    {
        perms = ""
        for (i = 1; i <= NF; i++) {
            if (length($i) == 10 && match($i, /^[-dlcbps]/)) { perms = $i; break }
        }
        if (perms == "") next
        name = $NF
        if (name == "." || name == "..") next
        type = (substr(perms, 1, 1) == "d") ? "D" : "F"
        print type, name
    }' | while read -r type name; do
        case "$type" in
            D) walk "$src/$name" "$dst/$name" ;;
            F) e2cp "$IMAGE:$src/$name" "$dst/$name" 2>/dev/null || true ;;
        esac
    done
}

walk /home "$STAGING/home"
walk /tmp  "$STAGING/tmp"
echo "[disk] preserved /home and /tmp from $IMAGE"
exit 0
