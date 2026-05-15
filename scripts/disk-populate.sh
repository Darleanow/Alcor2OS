#!/bin/sh
# scripts/disk-populate.sh — populate the Alcor2 guest disk image.
#
# Called by `make disk-populate` after the disk is mounted. All paths are
# relative to the project root. The first argument is the mount point (mnt/).
# The second argument is the optional sudo prefix (empty for fuse2fs).
#
# Layout produced on the guest:
#
#   /bin/        shell … font-demo (+ Fira Code TTF when built), cc-wrapper …
#   /usr/bin/    cxx (alias of cc-wrapper)
#   /usr/lib/    crt1.o … libc.a libncurses.a libtinfo.a libgcc*.a libstdc++.a …
#   /usr/include musl + ncurses headers (curses.h, …)
#   /usr/share/terminfo   optional — from host `make ncurses`
#   /etc/profile TERM default + hints
#   /home/       sample sources (simple.c, hi.c, ncurses-demo.c when ncurses present)
#   /etc/motd    welcome banner

set -eu

MNT=${1:?mount point}
S=${2:-} # sudo prefix or empty

# Project root (works even if the shell’s cwd is not the repo).
ROOT=$(cd "$(dirname "$0")/.." && pwd)
USER_BUILD=$ROOT/user/build
MUSL=$ROOT/thirdparty/musl/install
MUSL_CROSS=$ROOT/thirdparty/musl-cross
MUSL_SYSROOT=$MUSL_CROSS/x86_64-linux-musl
CLANG_INSTALL=$ROOT/thirdparty/clang-install
NCURSES=$ROOT/thirdparty/ncurses-install
LLVM_BUILD=$ROOT/thirdparty/llvm-build
BUILD=$ROOT/build

CLANG_BIN=
for _cand in "$CLANG_INSTALL"/usr/bin/clang-[0-9]*; do
  if [ -f "$_cand" ]; then
    CLANG_BIN=$_cand
    break
  fi
done
if [ -z "$CLANG_BIN" ] || [ ! -f "$CLANG_BIN" ]; then
  _plain="$CLANG_INSTALL/usr/bin/clang"
  [ -f "$_plain" ] && CLANG_BIN=$_plain
fi
unset _cand _plain
LLD_BIN=$CLANG_INSTALL/usr/bin/lld

# ----- 1. Skeleton -----------------------------------------------------------
$S mkdir -p \
  "$MNT/bin" "$MNT/etc" "$MNT/tmp" "$MNT/home" \
  "$MNT/usr/bin" "$MNT/usr/include" "$MNT/usr/lib" \
  "$MNT/usr/lib/clang"

# Sample sources for testing the in-OS toolchains.
printf 'int main(void){return 0;}\n' \
  | $S tee "$MNT/home/simple.c" >/dev/null
printf '#include <stdio.h>\nint main(void){printf("hi\\n");return 0;}\n' \
  | $S tee "$MNT/home/hi.c" >/dev/null

# ----- 2. Builtins (ls/cat/echo/...) and shell apps --------------------------
# Wipe everything in /bin except heavy toolchain binaries we may keep cached.
$S find "$MNT/bin" -mindepth 1 -maxdepth 1 \
  ! -name clang.real ! -name lld -exec rm -f {} +

for f in "$USER_BUILD/bin"/*.elf "$USER_BUILD/apps"/*.elf; do
  [ -f "$f" ] || continue
  bn=$(basename "$f" .elf)
  # `cc` is our explicit compiler-driver wrapper — installed under multiple names below.
  [ "$bn" = cc ] && continue
  $S cp "$f" "$MNT/bin/$bn"
done

# Fonts for font-demo (FreeType + HarfBuzz on guest).
fd_fira="$USER_BUILD/apps/font-demo/FiraCode-Regular.ttf"
[ -f "$fd_fira" ] && $S cp "$fd_fira" "$MNT/bin/FiraCode-Regular.ttf"

# ----- 3a. User-space OS library headers -------------------------------------
$S mkdir -p "$MNT/usr/include"
$S cp "$ROOT/user/include/grendizer.h" "$MNT/usr/include/grendizer.h"
[ -f "$USER_BUILD/lib/libgrendizer.a" ] && $S cp "$USER_BUILD/lib/libgrendizer.a" "$MNT/usr/lib/libgrendizer.a" || true

# ----- 3. musl runtime ---------------------------------------------------------
$S cp -r "$MUSL/include/."          "$MNT/usr/include/"
$S cp    "$MUSL/lib/libc.a"         "$MNT/usr/lib/libc.a"

# Sanity check — if this fails the disk image is unusable for the toolchain.
[ -f "$MNT/usr/include/stdio.h" ] || {
  echo >&2 "[disk-populate] FATAL: $MNT/usr/include/stdio.h missing after musl copy."
  echo >&2 "  Source dir: $MUSL/include — run 'make musl' to (re)build it."
  exit 1
}

# Bundle Alcor's crt0.o into a single relocatable named crt1.o so Clang/lld
# pick it up under the standard musl name. (BUILD/ is not always present —
# e.g. after `make clean` or a fresh clone that never ran `make all`.)
mkdir -p "$BUILD"
ld -r "$USER_BUILD/crt/crt0.o" -o "$BUILD/crt1.o"
$S cp "$BUILD/crt1.o"          "$MNT/usr/lib/crt1.o"
$S cp "$MUSL/lib/crti.o"       "$MNT/usr/lib/crti.o"
$S cp "$MUSL/lib/crtn.o"       "$MNT/usr/lib/crtn.o"

# Optional musl extras (pthread/m/dl/rt). Harmless if missing.
for lib in libm libpthread librt libdl; do
  src=$MUSL/lib/$lib.a
  [ -f "$src" ] && $S cp "$src" "$MNT/usr/lib/$lib.a" || true
done

# ----- 4. Clang/LLD toolchain (optional, requires `make clang`) -------------
if [ -n "$CLANG_BIN" ] && [ -f "$CLANG_BIN" ]; then
  # 4a. Real clang + lld binaries. The wrapper at /bin/clang execs clang.real,
  # so we never expose the bare clang to the user.
  $S cp "$CLANG_BIN" "$MNT/bin/clang.real"
  $S strip "$MNT/bin/clang.real" 2>/dev/null || true
  if [ -f "$LLD_BIN" ]; then
    $S cp "$LLD_BIN" "$MNT/bin/lld"
    $S strip "$MNT/bin/lld" 2>/dev/null || true
    $S ln -sf lld "$MNT/bin/ld.lld"
    $S ln -sf lld "$MNT/bin/ld"
    $S ln -sf ../../bin/lld "$MNT/usr/bin/ld"
    $S ln -sf ../../bin/lld "$MNT/usr/bin/ld.lld"
  fi

  # 4b. Clang resource headers (stddef.h, stdint.h, intrinsics, …). Live in
  # the build tree, NOT in clang-install — `cmake --install --component clang`
  # doesn't ship `clang-resource-headers`.
  if [ -d "$LLVM_BUILD/lib/clang" ]; then
    $S cp -r "$LLVM_BUILD/lib/clang/." "$MNT/usr/lib/clang/"
  fi

  # 4c. libstdc++ headers + library, libgcc, crtbegin/crtend (from musl-cross).
  if [ -d "$MUSL_SYSROOT/include/c++" ]; then
    $S mkdir -p "$MNT/usr/include/c++"
    $S cp -r "$MUSL_SYSROOT/include/c++/." "$MNT/usr/include/c++/"
  fi
  for name in libstdc++.a libstdc++fs.a libsupc++.a; do
    src=$(find "$MUSL_SYSROOT/lib" -maxdepth 3 -name "$name" 2>/dev/null | head -1)
    [ -n "$src" ] && $S cp "$src" "$MNT/usr/lib/$name" || true
  done
  for name in libgcc.a libgcc_eh.a; do
    src=$(find "$MUSL_CROSS/lib" -maxdepth 5 -name "$name" 2>/dev/null | head -1)
    [ -n "$src" ] && $S cp "$src" "$MNT/usr/lib/$name" || true
  done
  [ -f "$MNT/usr/lib/libgcc.a" ] && $S cp "$MNT/usr/lib/libgcc.a" "$MNT/usr/lib/libgcc_s.a" || true
  for pat in 'crtbegin*.o' 'crtend*.o'; do
    find "$MUSL_CROSS/lib" -maxdepth 5 -name "$pat" 2>/dev/null \
      | while read -r f; do $S cp "$f" "$MNT/usr/lib/"; done
  done

  # 4d. Wrapper aliases. `cc` is our explicit driver (user/bin/cc.c); install
  # it under every common name. No /bin/clang.cfg, no auto-detection.
  if [ -f "$USER_BUILD/bin/cc.elf" ]; then
    $S cp "$USER_BUILD/bin/cc.elf" "$MNT/bin/cc"
    for alias in clang c++ g++ gcc clang++; do
      $S ln -sf cc "$MNT/bin/$alias"
    done
    $S ln -sf ../../bin/cc "$MNT/usr/bin/cxx"
  fi
else
  echo "[disk] no static clang installed (run: make clang)"
fi

# ----- 4e. ncurses -----------------------------
# Staged tree should be thirdparty/ncurses-install/usr/share/terminfo (mk/thirdparty.mk:
# install with DESTDIR + --prefix=/usr). Old installs only put terminfo on the *host*
# /usr; we still copy minimal entries from the dev machine so TERM=xterm-256color works.
copy_terminfo_entry_from_host()
{
  name=$1
  found=$(find /usr/share/terminfo /lib/terminfo -name "$name" -type f 2>/dev/null | head -1)
  [ -n "$found" ] || return 1
  bucket=$(basename "$(dirname "$found")")
  $S mkdir -p "$MNT/usr/share/terminfo/$bucket"
  $S cp "$found" "$MNT/usr/share/terminfo/$bucket/"
  echo "[disk] terminfo fallback: copied $name from host ($found)"
  return 0
}

if [ -f "$NCURSES/usr/lib/libncurses.a" ]; then
  echo "[disk] ncurses static libs + headers (terminfo from staging or host fallback)"
  $S mkdir -p "$MNT/usr/share/terminfo"
  $S cp "$NCURSES/usr/lib/libncurses.a" "$MNT/usr/lib/"
  $S cp "$NCURSES/usr/lib/libtinfo.a" "$MNT/usr/lib/"
  if [ -d "$NCURSES/usr/include" ]; then
    $S cp -r "$NCURSES/usr/include/." "$MNT/usr/include/"
  fi
  if [ -d "$NCURSES/usr/share/terminfo" ] && \
     find "$NCURSES/usr/share/terminfo" -type f -print -quit 2>/dev/null | grep -q .; then
    $S cp -r "$NCURSES/usr/share/terminfo/." "$MNT/usr/share/terminfo/"
  else
    echo "[disk] WARN: thirdparty/ncurses-install/usr/share/terminfo missing or empty (broken staging)."
    echo "[disk]       Fix: rm -rf thirdparty/ncurses-install && make ncurses"
  fi
  if ! find "$MNT/usr/share/terminfo" -name 'xterm-256color' -print -quit 2>/dev/null | grep -q .; then
    copy_terminfo_entry_from_host xterm-256color || true
  fi
  if ! find "$MNT/usr/share/terminfo" -name 'vt100' -print -quit 2>/dev/null | grep -q .; then
    copy_terminfo_entry_from_host vt100 || true
  fi
  if ! find "$MNT/usr/share/terminfo" -name 'xterm-256color' -print -quit 2>/dev/null | grep -q .; then
    echo "[disk] WARN: no xterm-256color on guest — install ncurses terminfo on this host, or rebuild thirdparty/ncurses-install"
  fi
  $S tee "$MNT/home/ncurses-demo.c" >/dev/null <<'EOF'
/* Build: cc ncurses-demo.c -lncurses -ltinfo   Run from a real TTY. */
#include <curses.h>

int main(void)
{
  if(!initscr())
    return 1;
  raw();
  noecho();
  curs_set(0);
  mvprintw(2, 2, "ncurses on Alcor2 — any key to exit");
  refresh();
  (void)getch();
  endwin();
  return 0;
}
EOF
else
  echo "[disk] ncurses skipped (host: make ncurses, then disk-populate again)"
fi

$S tee "$MNT/etc/profile" >/dev/null <<'EOF'
# Alcor2 login environment — use: . /etc/profile
export TERM="${TERM:-xterm-256color}"
export TERMINFO="${TERMINFO:-/usr/share/terminfo}"
EOF

# ----- 5. Welcome banner -----------------------------------------------------
NC_ON_DISK=0
[ -f "$MNT/usr/lib/libncurses.a" ] && NC_ON_DISK=1

{
  echo "Welcome to Alcor2!"
  echo "  C   : cc file.c     (or clang)"
  echo "  C++ : c++ file.cpp  (or g++ / cxx)"
  echo "  Run : ./a.out"
  echo "  tip : . /etc/profile   # TERM for ncurses"
  if [ "$NC_ON_DISK" = 1 ]; then
    echo "  TUI : cc ui.c -lncurses -ltinfo   (sample: /home/ncurses-demo.c)"
  fi
} | $S tee "$MNT/etc/motd" >/dev/null
