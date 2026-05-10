# Bootstrap third-party toolchains (under thirdparty/, gitignored)

# On macOS, host clang/cctools can't build musl: configure auto-detects
# aarch64-darwin and Mach-O rejects weak_alias. Force the x86_64 cross.
# CROSS_COMPILE is set so AR/RANLIB resolve to x86_64-elf-{ar,ranlib}
# (Homebrew binutils) instead of musl's default x86_64-linux-musl-ar.
ifeq ($(UNAME), Darwin)
  MUSL_CONFIGURE_EXTRA := --target=x86_64-linux-musl \
                          CC=$(CC) CROSS_COMPILE=x86_64-elf-
else
  MUSL_CONFIGURE_EXTRA :=
endif

PKGCONFIG := $(shell command -v pkg-config 2>/dev/null)

.PHONY: musl tcc clang musl-cross ncurses ncurses-verify freetype harfbuzz

musl:  thirdparty/musl/$(MUSL_PREFIX)/lib/libc.a
tcc:   thirdparty/tcc-install/usr/bin/tcc
clang: thirdparty/clang-install/usr/bin/clang
musl-cross: thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
# Phony ncurses is always “out of date”; ncurses-verify runs every time so a stale
# install (lib present but terminfo missing — old --prefix-without-DESTDIR bug)
# fails fast with a clear fix command.
ncurses: ncurses-verify thirdparty/ncurses-install/usr/lib/libncurses.a

ncurses-verify:
	@lib="$(CURDIR)/thirdparty/ncurses-install/usr/lib/libncurses.a"; \
	ti="$(CURDIR)/thirdparty/ncurses-install/usr/share/terminfo"; \
	if [ -f "$$lib" ]; then \
	  if [ ! -d "$$ti" ] || ! find "$$ti" -name 'xterm-256color' -print -quit 2>/dev/null | grep -q .; then \
	    echo >&2 "ncurses install under thirdparty/ncurses-install is missing usr/share/terminfo (terminfo was installed to the host /usr without DESTDIR)."; \
	    echo >&2 "Fix: rm -rf thirdparty/ncurses-install && make ncurses   then: make disk-resync (or disk-populate)"; \
	    exit 1; \
	  fi; \
	fi
freetype: thirdparty/freetype-install/usr/lib/libfreetype.a
harfbuzz: thirdparty/harfbuzz-install/usr/lib/libharfbuzz.a

thirdparty/limine/limine:
	git clone $(LIMINE_URL) --branch=$(LIMINE_REV) --depth=1 thirdparty/limine
	$(MAKE) -C thirdparty/limine

thirdparty/musl/$(MUSL_PREFIX)/lib/libc.a:
	@echo "musl $(MUSL_VER) — download & build"
	@mkdir -p thirdparty
	@curl -sL $(MUSL_URL) | tar xz -C thirdparty
	@mv thirdparty/musl-$(MUSL_VER) thirdparty/musl
	@cd thirdparty/musl && ./configure --prefix=$$(pwd)/$(MUSL_PREFIX) \
		--disable-shared $(MUSL_CONFIGURE_EXTRA) \
		CFLAGS='-Os -fno-stack-protector' >/dev/null
	@$(MAKE) -C thirdparty/musl -j$(JOBS) >/dev/null
	@$(MAKE) -C thirdparty/musl install >/dev/null

thirdparty/tcc-install/usr/bin/tcc: thirdparty/musl/$(MUSL_PREFIX)/lib/libc.a
	@echo "TCC $(TCC_VER) — download & build"
	@mkdir -p thirdparty
	@rm -rf thirdparty/tcc-src
	@curl -sL $(TCC_URL) | tar xj -C thirdparty
	@mv thirdparty/tcc-$(TCC_VER) thirdparty/tcc-src
	@cd thirdparty/tcc-src && \
		./configure \
			--prefix=/usr \
			--cc=$(CURDIR)/thirdparty/musl/$(MUSL_PREFIX)/bin/musl-gcc \
			--extra-ldflags="-static" \
			--cpu=x86_64 \
			--config-musl \
			--sysincludepaths=$(CURDIR)/thirdparty/musl/$(MUSL_PREFIX)/include \
			>/dev/null 2>&1 && \
		make -j$(JOBS) 2>&1 | tail -5 && \
		make install DESTDIR=$(CURDIR)/thirdparty/tcc-install >/dev/null 2>&1

thirdparty/musl-cross/bin/x86_64-linux-musl-gcc:
	@echo "musl-cross (x86_64-linux-musl) — long build, log: thirdparty/musl-cross-build.log"
	@mkdir -p thirdparty
	@rm -rf thirdparty/musl-cross-src
	@curl -sL $(MUSL_CROSS_URL) | tar xz -C thirdparty
	@mv thirdparty/musl-cross-make-master thirdparty/musl-cross-src
	@echo 'TARGET = x86_64-linux-musl'                                   >  thirdparty/musl-cross-src/config.mak
	@echo 'OUTPUT = $(CURDIR)/thirdparty/musl-cross'                     >> thirdparty/musl-cross-src/config.mak
	@echo 'COMMON_CONFIG += --disable-nls'                               >> thirdparty/musl-cross-src/config.mak
	@echo 'GCC_CONFIG += --enable-languages=c,c++ --disable-multilib'    >> thirdparty/musl-cross-src/config.mak
	@$(MAKE) -C thirdparty/musl-cross-src -j$(JOBS) install \
	  2>&1 | tee thirdparty/musl-cross-build.log | tail -10
	@test -f thirdparty/musl-cross/bin/x86_64-linux-musl-gcc || \
	  { echo >&2 "musl-cross failed — see thirdparty/musl-cross-build.log"; exit 1; }

# Curses + terminfo (libtinfo): static *.a for x86_64-linux-musl. ~1–2 min after configure.
# Install MUST use DESTDIR: misc/run_tic.sh sets TERMINFO="${DESTDIR}/usr/share/terminfo"
# (ticdir is absolute /usr/share/terminfo). With empty DESTDIR, terminfo lands on the host and
# thirdparty/ncurses-install never gets share/terminfo — guest TUIs break.
thirdparty/ncurses-install/usr/lib/libncurses.a: thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
	@echo "ncurses $(NCURSES_VER) — static libraries for x86_64-linux-musl (log: thirdparty/ncurses-build.log)"
	@mkdir -p thirdparty
	@rm -rf thirdparty/ncurses-install thirdparty/ncurses-src
	@curl -sL $(NCURSES_URL) | tar xz -C thirdparty
	@mv thirdparty/ncurses-$(NCURSES_VER) thirdparty/ncurses-src
	@cd thirdparty/ncurses-src && \
	  BUILD_CC=cc \
	  PKG_CONFIG=/bin/false \
	  ./configure \
	    --host=x86_64-linux-musl \
	    --prefix=/usr \
	    --with-build-cc=cc \
	    --without-ada \
	    --without-cxx \
	    --without-cxx-binding \
	    --without-progs \
	    --without-tests \
	    --without-gpm \
	    --without-debug \
	    --without-manpages \
	    --without-developer \
	    --disable-rpath-hack \
	    --with-termlib \
	    --with-default-terminfo-dir=/usr/share/terminfo \
	    --with-fallbacks=xterm-256color,vt100 \
	    --disable-shared \
	    --enable-static \
	    --enable-overwrite \
	    --enable-pc-files=no \
	    CC=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-gcc \
	    CXX=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-g++ \
	    AR=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-ar \
	    RANLIB=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-ranlib \
	    STRIP=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-strip \
	    CFLAGS='-Os -fno-stack-protector' \
	    >$(CURDIR)/thirdparty/ncurses-config.log 2>&1 || \
	    { tail -30 $(CURDIR)/thirdparty/ncurses-config.log; echo >&2 "ncurses configure failed"; exit 1; }
	@$(MAKE) -C thirdparty/ncurses-src -j$(JOBS) \
	  >$(CURDIR)/thirdparty/ncurses-build.log 2>&1 || \
	  { tail -40 $(CURDIR)/thirdparty/ncurses-build.log; echo >&2 "ncurses build failed"; exit 1; }
	@$(MAKE) -C thirdparty/ncurses-src install DESTDIR=$(CURDIR)/thirdparty/ncurses-install \
	  >$(CURDIR)/thirdparty/ncurses-install.log 2>&1 || \
	  { tail -40 $(CURDIR)/thirdparty/ncurses-install.log; echo >&2 "ncurses install failed"; exit 1; }
	@test -f $(CURDIR)/thirdparty/ncurses-install/usr/lib/libncurses.a || \
	  { echo >&2 "expected libncurses.a"; exit 1; }
	@test -f $(CURDIR)/thirdparty/ncurses-install/usr/include/curses.h || \
	  test -f $(CURDIR)/thirdparty/ncurses-install/usr/include/ncurses/curses.h || \
	  { echo >&2 "ncurses headers missing — see thirdparty/ncurses-install.log"; exit 1; }
	@test -d $(CURDIR)/thirdparty/ncurses-install/usr/share/terminfo || \
	  { echo >&2 "terminfo dir missing after install — see thirdparty/ncurses-install.log (host needs /usr/bin/tic)"; exit 1; }
	@find $(CURDIR)/thirdparty/ncurses-install/usr/share/terminfo -name 'xterm-256color' -print -quit | grep -q . || \
	  { echo >&2 "xterm-256color terminfo missing in staged tree"; exit 1; }
	@echo "ncurses → thirdparty/ncurses-install/usr/{include,lib,share/terminfo}"

# FreeType (static, minimal deps) for guest font rasterization.
thirdparty/freetype-install/usr/lib/libfreetype.a: thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
	@echo "FreeType $(FREETYPE_VER) → thirdparty/freetype-install (log: thirdparty/freetype-build.log)"
	@command -v pkg-config >/dev/null || { echo >&2 "need pkg-config for HarfBuzz"; exit 1; }
	@mkdir -p thirdparty
	@rm -rf thirdparty/freetype-src
	@curl -fsSL $(FREETYPE_URL) | tar xJ -C thirdparty
	@mv thirdparty/freetype-$(FREETYPE_VER) thirdparty/freetype-src
	@cd thirdparty/freetype-src && \
	  PKG_CONFIG=/bin/false \
	  ./configure \
	    --host=x86_64-linux-musl \
	    --prefix=$(CURDIR)/thirdparty/freetype-install/usr \
	    --disable-shared --enable-static \
	    --without-zlib --without-bzip2 --without-png --without-brotli --without-harfbuzz \
	    CC=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-gcc \
	    AR=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-ar \
	    RANLIB=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-ranlib \
	    CFLAGS='-Os -fno-stack-protector' \
	    >$(CURDIR)/thirdparty/freetype-config.log 2>&1 || \
	    { tail -40 $(CURDIR)/thirdparty/freetype-config.log; exit 1; }
	@$(MAKE) -C thirdparty/freetype-src -j$(JOBS) \
	  >$(CURDIR)/thirdparty/freetype-build.log 2>&1 || \
	    { tail -40 $(CURDIR)/thirdparty/freetype-build.log; exit 1; }
	@$(MAKE) -C thirdparty/freetype-src install \
	  >$(CURDIR)/thirdparty/freetype-install.log 2>&1 || \
	    { tail -20 $(CURDIR)/thirdparty/freetype-install.log; exit 1; }
	@test -f $(CURDIR)/thirdparty/freetype-install/usr/lib/libfreetype.a || exit 1

# HarfBuzz (static, FreeType only): host needs `meson` + `ninja`.
thirdparty/harfbuzz-install/usr/lib/libharfbuzz.a: thirdparty/freetype-install/usr/lib/libfreetype.a
	@echo "HarfBuzz $(HARFBUZZ_VER) → thirdparty/harfbuzz-install (log: thirdparty/harfbuzz-build.log)"
	$(if $(PKGCONFIG),,$(error harfbuzz: install pkg-config \(apt: pkg-config, brew: pkgconf\)))
	@command -v meson >/dev/null 2>&1 || { echo >&2 "install meson (pip install meson)"; exit 1; }
	@command -v ninja >/dev/null 2>&1 || { echo >&2 "install ninja (ninja-build package)"; exit 1; }
	@rm -rf thirdparty/harfbuzz-src thirdparty/harfbuzz-build
	@curl -fsSL $(HARFBUZZ_URL) | tar xJ -C thirdparty
	@mv thirdparty/harfbuzz-$(HARFBUZZ_VER) thirdparty/harfbuzz-src
	@printf '%s\n' \
	  "[binaries]" \
	  "c = '$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-gcc'" \
	  "cpp = '$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-g++'" \
	  "ar = '$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-ar'" \
	  "strip = '$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl-strip'" \
	  "pkg-config = '$(PKGCONFIG)'" \
	  "" \
	  "[built-in options]" \
	  "default_library = 'static'" \
	  "c_args = ['-Os', '-fno-stack-protector']" \
	  "cpp_args = ['-Os', '-fno-stack-protector']" \
	  "" \
	  "[host_machine]" \
	  "system = 'linux'" \
	  "cpu_family = 'x86_64'" \
	  "cpu = 'x86_64'" \
	  "endian = 'little'" \
	  "" \
	  "[properties]" \
	  "pkg_config_libdir = '$(CURDIR)/thirdparty/freetype-install/usr/lib/pkgconfig'" \
	  > $(CURDIR)/thirdparty/cross-musl-meson.ini
	@PKG_CONFIG_LIBDIR=$(CURDIR)/thirdparty/freetype-install/usr/lib/pkgconfig \
	  meson setup $(CURDIR)/thirdparty/harfbuzz-build $(CURDIR)/thirdparty/harfbuzz-src \
	    --cross-file $(CURDIR)/thirdparty/cross-musl-meson.ini \
	    --prefix=/usr \
	    -Ddefault_library=static \
	    -Dfreetype=enabled \
	    -Dcairo=disabled \
	    -Dchafa=disabled \
	    -Dicu=disabled \
	    -Dglib=disabled \
	    -Dgobject=disabled \
	    -Dtests=disabled \
	    -Ddocs=disabled \
	    -Dutilities=disabled \
	    -Dbenchmark=disabled \
	    -Dintrospection=disabled \
	    >$(CURDIR)/thirdparty/harfbuzz-config.log 2>&1 || \
	    { tail -50 $(CURDIR)/thirdparty/harfbuzz-config.log; exit 1; }
	@PKG_CONFIG_LIBDIR=$(CURDIR)/thirdparty/freetype-install/usr/lib/pkgconfig \
	  meson compile -C $(CURDIR)/thirdparty/harfbuzz-build -j$(JOBS) \
	    >$(CURDIR)/thirdparty/harfbuzz-build.log 2>&1 || \
	    { tail -50 $(CURDIR)/thirdparty/harfbuzz-build.log; exit 1; }
	@DESTDIR=$(CURDIR)/thirdparty/harfbuzz-install \
	  meson install -C $(CURDIR)/thirdparty/harfbuzz-build \
	    >$(CURDIR)/thirdparty/harfbuzz-install.log 2>&1 || \
	    { tail -40 $(CURDIR)/thirdparty/harfbuzz-install.log; exit 1; }
	@test -f $(CURDIR)/thirdparty/harfbuzz-install/usr/lib/libharfbuzz.a || exit 1

# Static clang+lld for the guest disk (requires musl-cross).
CLANG_BIN := $(firstword $(wildcard thirdparty/clang-install/usr/bin/clang-[0-9]*))

thirdparty/clang-install/usr/bin/clang: thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
	@echo "LLVM $(LLVM_VER) (~700 MB tarball) — configure & build Ninja targets clang+lld"
	@mkdir -p thirdparty
	@rm -rf thirdparty/llvm-src thirdparty/llvm-build
	@curl -L $(LLVM_URL) | tar xJ -C thirdparty
	@mv thirdparty/llvm-project-$(LLVM_VER).src thirdparty/llvm-src
	@CROSS=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl; \
	mkdir -p thirdparty/llvm-build && \
	cmake -S thirdparty/llvm-src/llvm \
	  -B thirdparty/llvm-build \
	  -G Ninja \
	  -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DCMAKE_INSTALL_PREFIX=/usr \
	  -DCMAKE_C_COMPILER="$$CROSS-gcc" \
	  -DCMAKE_CXX_COMPILER="$$CROSS-g++" \
	  -DCMAKE_ASM_COMPILER="$$CROSS-gcc" \
	  -DCMAKE_AR="$$CROSS-ar" \
	  -DCMAKE_RANLIB="$$CROSS-ranlib" \
	  -DCMAKE_NM="$$CROSS-nm" \
	  -DCMAKE_STRIP="$$CROSS-strip" \
	  -DCMAKE_EXE_LINKER_FLAGS="-static" \
	  -DCMAKE_SHARED_LIBRARY_LINK_C_FLAGS="-static" \
	  -DLLVM_TARGETS_TO_BUILD="X86" \
	  -DLLVM_ENABLE_PROJECTS="clang;lld" \
	  -DLLVM_BUILD_STATIC=ON \
	  -DLLVM_ENABLE_ZLIB=OFF \
	  -DLLVM_ENABLE_ZSTD=OFF \
	  -DLLVM_ENABLE_LIBXML2=OFF \
	  -DLLVM_ENABLE_TERMINFO=OFF \
	  -DLLVM_ENABLE_LIBEDIT=OFF \
	  -DLLVM_ENABLE_ASSERTIONS=OFF \
	  -DLLVM_INCLUDE_TESTS=OFF \
	  -DLLVM_INCLUDE_EXAMPLES=OFF \
	  -DLLVM_INCLUDE_BENCHMARKS=OFF \
	  -DCLANG_ENABLE_ARCMT=OFF \
	  -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
	  2>&1 | tail -10
	@cmake --build thirdparty/llvm-build --target clang lld -j$(JOBS) 2>&1 | tail -20
	@DESTDIR=$(CURDIR)/thirdparty/clang-install cmake --install thirdparty/llvm-build \
	  --component clang 2>&1 | tail -3
	@DESTDIR=$(CURDIR)/thirdparty/clang-install cmake --install thirdparty/llvm-build \
	  --component lld 2>&1 | tail -3
