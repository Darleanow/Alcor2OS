# Third-party tarball / clone metadata

LIMINE_URL := https://github.com/limine-bootloader/limine.git
LIMINE_REV := v9.x-binary

MUSL_URL := https://git.musl-libc.org/cgit/musl/snapshot/musl-1.2.5.tar.gz
MUSL_VER := 1.2.5

MUSL_CROSS_URL := https://github.com/richfelker/musl-cross-make/archive/refs/heads/master.tar.gz

# Static ncurses for userland (requires musl-cross). Guest: set TERM=xterm-256color (fallbacks compiled in).
NCURSES_VER := 6.4
NCURSES_URL := https://invisible-mirror.net/archives/ncurses/ncurses-$(NCURSES_VER).tar.gz

LLVM_VER := 18.1.8
LLVM_URL := https://github.com/llvm/llvm-project/releases/download/llvmorg-$(LLVM_VER)/llvm-project-$(LLVM_VER).src.tar.xz

# Static text stack for userland (musl-cross). Requires host `meson` + `ninja`.
FREETYPE_VER := 2.13.3
FREETYPE_URL := https://download.savannah.gnu.org/releases/freetype/freetype-$(FREETYPE_VER).tar.xz

HARFBUZZ_VER := 8.3.0
HARFBUZZ_URL := https://github.com/harfbuzz/harfbuzz/releases/download/$(HARFBUZZ_VER)/harfbuzz-$(HARFBUZZ_VER).tar.xz
