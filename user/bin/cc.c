/**
 * @file user/bin/cc.c
 * @brief Alcor2 C/C++ compiler driver.
 *
 * Single binary installed on the guest as `/bin/clang`, `/bin/cc`, `/bin/c++`,
 * `/bin/g++`, and `/usr/bin/cxx`. Forwards to the real Clang at
 * `/bin/clang.real` with a fully explicit command line — no `clang.cfg`, no
 * toolchain auto-detection. Mode (C vs C++) is derived from argv[0] and the
 * input extensions, so:
 *
 *   $ clang  hello.c       → C   compile + link → ./a.out
 *   $ cc     hello.c       → C   compile + link → ./a.out
 *   $ c++    hello.cpp     → C++ compile + link → ./a.out
 *   $ g++    hello.cpp     → C++ compile + link → ./a.out
 *
 * All link inputs (crt1.o, crti.o, libc.a, libgcc.a, crtn.o, …) live under
 * /usr/lib and are passed by absolute path so Clang never has to guess.
 *
 * argv[0] forwarded to clang.real must be an absolute path: LLVM’s
 * getMainExecutable() reads /proc/self/exe (we have no procfs) then falls back
 * to PATH lookup on basename-only argv[0]. Without PATH, it returns "" and
 * posix_spawn fails with EACCES (“Permission denied”).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REAL_CLANG "/bin/clang.real"

/* Canonical paths on the Alcor2 guest disk. Kept in one place so the wrapper
 * and the disk-populate target agree on the layout. */
#define SYS_LIB       "/usr/lib"
#define SYS_INC       "/usr/include"
#define CLANG_RES_INC "/usr/lib/clang/18/include"
/* Must match libstdc++ layout installed by scripts/disk-populate.sh
 * (musl-cross). */
#define CXX_INC        "/usr/include/c++/9.4.0"
#define CXX_INC_TARGET "/usr/include/c++/9.4.0/x86_64-linux-musl"

/**
 * @brief Detect whether we should run in C++ mode.
 *
 * Triggered by argv[0] (c++, g++, cxx, clang++) or by any input file with a
 * C++ extension (.cpp, .cc, .cxx, .c++, .C). Plain `clang` / `cc` on `.c`
 * stays in C mode.
 */
static int wants_cxx(int argc, char *argv[])
{
  const char *prog = argv[0] ? argv[0] : "";
  const char *base = strrchr(prog, '/');
  base             = base ? base + 1 : prog;

  if(strstr(base, "++") || !strcmp(base, "cxx") || !strcmp(base, "g++"))
    return 1;

  for(int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if(!a || a[0] == '-')
      continue;
    const char *dot = strrchr(a, '.');
    if(!dot)
      continue;
    if(!strcmp(dot, ".cpp") || !strcmp(dot, ".cc") || !strcmp(dot, ".cxx") ||
       !strcmp(dot, ".c++") || !strcmp(dot, ".C"))
      return 1;
  }
  return 0;
}

/**
 * @brief Append a single argument to the forwarded argv.
 */
static void push(char **fwd, int *n, const char *a)
{
  fwd[(*n)++] = (char *)a;
}

int main(int argc, char *argv[])
{
  /* Worst-case size: our own injected flags (~24) + every user arg. */
  int    cap = argc + 32;
  char **fwd = malloc((size_t)cap * sizeof(*fwd));
  if(!fwd) {
    fprintf(stderr, "cc: out of memory\n");
    return 1;
  }

  int cxx = wants_cxx(argc, argv);
  int n   = 0;

  /* argv[0]: absolute path + driver basename for mode (see file comment). */
  push(fwd, &n, cxx ? "/bin/clang++" : "/bin/clang");

  /* Target + linker. Static link against musl, lld is /bin/lld.
   * Force libstdc++ for C++ — Clang's default for the musl target is libc++,
   * which we don't ship; libstdc++ comes from musl-cross gcc. */
  push(fwd, &n, "--target=x86_64-linux-musl");
  push(fwd, &n, "-fuse-ld=lld");
  push(fwd, &n, "-static");
  if(cxx)
    push(fwd, &n, "-stdlib=libstdc++");

  /* Tell Clang where to look for crt files (-B/usr/lib for crt1.o etc., -B/bin
   * for ld.lld). No --sysroot — we provide every search path explicitly below
   * so Clang's auto-detection can't sneak in surprise directories that would
   * cause include loops. */
  push(fwd, &n, "-B" SYS_LIB);
  push(fwd, &n, "-B/bin");

  /* Headers — fully explicit. -nostdinc disables ALL of Clang's default
   * search paths (resource dir, target-derived sysroot includes, /usr/local,
   * etc.). We then add back exactly what's on disk, in the right order:
   *
   *   1. libstdc++ (only for C++; defines C++ headers like <iostream>)
   *   2. Clang resource dir (provides <stddef.h>, <stdint.h>, <stdarg.h>, …)
   *   3. musl libc (provides <stdio.h>, <string.h>, …)
   *
   * Order matters: libstdc++ first so its <stdio.h> wrapper (which does
   * #include_next <stdio.h>) properly chains down to musl's. */
  push(fwd, &n, "-nostdinc");
  if(cxx) {
    push(fwd, &n, "-isystem");
    push(fwd, &n, CXX_INC);
    push(fwd, &n, "-isystem");
    push(fwd, &n, CXX_INC_TARGET);
  }
  push(fwd, &n, "-isystem");
  push(fwd, &n, CLANG_RES_INC);
  push(fwd, &n, "-isystem");
  push(fwd, &n, SYS_INC);

  /* Libraries are searched here. */
  push(fwd, &n, "-L" SYS_LIB);

  /* lld-specific tweaks: avoid mmap-output (filesystems with limited mmap
   * support) and threading (predictable single-threaded link). */
  push(fwd, &n, "-Wl,--threads=1");
  push(fwd, &n, "-Wl,--no-mmap-output-file");

  /* User arguments, verbatim. */
  for(int i = 1; i < argc; i++)
    push(fwd, &n, argv[i]);

  fwd[n] = NULL;

  execv(REAL_CLANG, fwd);
  fprintf(stderr, "cc: cannot exec %s\n", REAL_CLANG);
  return 127;
}
