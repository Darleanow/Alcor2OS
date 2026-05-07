# Third-party tarball / clone metadata

LIMINE_URL := https://github.com/limine-bootloader/limine.git
LIMINE_REV := v8.x-binary

MUSL_URL := https://git.musl-libc.org/cgit/musl/snapshot/musl-1.2.5.tar.gz
MUSL_VER := 1.2.5

TCC_URL := https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2
TCC_VER := 0.9.27

MUSL_CROSS_URL := https://github.com/richfelker/musl-cross-make/archive/refs/heads/master.tar.gz

# Static ncurses for userland (requires musl-cross). Guest: set TERM=xterm-256color (fallbacks compiled in).
NCURSES_VER := 6.4
NCURSES_URL := https://invisible-mirror.net/archives/ncurses/ncurses-$(NCURSES_VER).tar.gz

LLVM_VER := 18.1.8
LLVM_URL := https://github.com/llvm/llvm-project/releases/download/llvmorg-$(LLVM_VER)/llvm-project-$(LLVM_VER).src.tar.xz
