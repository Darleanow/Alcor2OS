# Bootstrap third-party toolchains (under thirdparty/, gitignored)

.PHONY: musl tcc clang musl-cross

musl:  thirdparty/musl/install/lib/libc.a
tcc:   thirdparty/tcc-install/usr/bin/tcc
clang: thirdparty/clang-install/usr/bin/clang
musl-cross: thirdparty/musl-cross/bin/x86_64-linux-musl-gcc

thirdparty/limine/limine:
	git clone $(LIMINE_URL) --branch=$(LIMINE_REV) --depth=1 thirdparty/limine
	$(MAKE) -C thirdparty/limine

thirdparty/musl/install/lib/libc.a:
	@echo "musl $(MUSL_VER) — download & build"
	@mkdir -p thirdparty
	@curl -sL $(MUSL_URL) | tar xz -C thirdparty
	@mv thirdparty/musl-$(MUSL_VER) thirdparty/musl
	@cd thirdparty/musl && ./configure --prefix=$$(pwd)/install \
		--disable-shared CFLAGS='-Os -fno-stack-protector' >/dev/null
	@$(MAKE) -C thirdparty/musl -j$$(nproc) >/dev/null
	@$(MAKE) -C thirdparty/musl install >/dev/null

thirdparty/tcc-install/usr/bin/tcc: thirdparty/musl/install/lib/libc.a
	@echo "TCC $(TCC_VER) — download & build"
	@mkdir -p thirdparty
	@rm -rf thirdparty/tcc-src
	@curl -sL $(TCC_URL) | tar xj -C thirdparty
	@mv thirdparty/tcc-$(TCC_VER) thirdparty/tcc-src
	@cd thirdparty/tcc-src && \
		./configure \
			--prefix=/usr \
			--cc=$(CURDIR)/thirdparty/musl/install/bin/musl-gcc \
			--extra-ldflags="-static" \
			--cpu=x86_64 \
			--config-musl \
			--sysincludepaths=$(CURDIR)/thirdparty/musl/install/include \
			>/dev/null 2>&1 && \
		make -j$$(nproc) 2>&1 | tail -5 && \
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
	@$(MAKE) -C thirdparty/musl-cross-src -j$$(nproc) install \
	  2>&1 | tee thirdparty/musl-cross-build.log | tail -10
	@test -f thirdparty/musl-cross/bin/x86_64-linux-musl-gcc || \
	  { echo >&2 "musl-cross failed — see thirdparty/musl-cross-build.log"; exit 1; }

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
	@cmake --build thirdparty/llvm-build --target clang lld -j$$(nproc) 2>&1 | tail -20
	@DESTDIR=$(CURDIR)/thirdparty/clang-install cmake --install thirdparty/llvm-build \
	  --component clang 2>&1 | tail -3
	@DESTDIR=$(CURDIR)/thirdparty/clang-install cmake --install thirdparty/llvm-build \
	  --component lld 2>&1 | tail -3
