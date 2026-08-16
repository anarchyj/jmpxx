/* SPDX-License-Identifier: MIT
 *
 * The platform hooks the unwinder asks a hosted system for, answered for a trusted
 * application that has none of them.
 *
 * OP-TEE's development kit supports a C++ trusted application by linking libstdc++ and
 * libgcc's unwinder, but the toolchain that ships with this workspace targets
 * aarch64-none-linux-gnu, so its unwinder was built against glibc and refers to glibc's
 * threading, environment, and dynamic-loader facilities. A trusted application has none
 * of them, and the link fails on the references rather than on anything the code does.
 *
 * Each hook below answers the question the unwinder is actually asking, for an
 * environment where the answer is fixed:
 *
 *   pthread_once and the condition-variable pair guard libgcc's frame-table cache
 *     against concurrent unwinds. A trusted application instance serves one invocation at
 *     a time, so the guard has nothing to protect: run the initializer once and let the
 *     waits return.
 *   _dl_find_object is glibc's fast path from a program counter to its exception frames.
 *     It is the one hook that has to do real work, and the comment above it says why.
 *   secure_getenv and the strtoul alias are reached by library initialization that a
 *     trusted application never uses, and answering emptily keeps them from pulling glibc
 *     in behind them.
 *
 * Using a bare-metal aarch64-none-elf toolchain would avoid all of this, because its
 * unwinder is built for a system with no threads and no loader. That toolchain is not in
 * this workspace, so the hooks stand in for it, and the trade is written down here rather
 * than left for the next reader to rediscover at the link step.
 */
#include <elf.h>
#include <link.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Each hook is a definition of a symbol the unwinder references, so each declares itself:
 * there is no header here that would, and a definition with no visible prototype is a
 * warning the build is right to raise. */
int pthread_once(int *control, void (*init)(void));
int pthread_cond_wait(void *cond, void *mutex);
int pthread_cond_broadcast(void *cond);
char *secure_getenv(const char *name);
unsigned long __isoc23_strtoul(const char *s, char **end, int base);

int pthread_once(int *control, void (*init)(void)) {
  if (control && *control == 0) {
    *control = 1;
    if (init) init();
  }
  return 0;
}

int pthread_cond_wait(void *cond, void *mutex) {
  (void)cond;
  (void)mutex;
  return 0;
}

int pthread_cond_broadcast(void *cond) {
  (void)cond;
  return 0;
}

/* The unwinder locates a frame's exception tables by asking which loaded object covers the
 * program counter, and a toolchain built against a recent glibc asks through
 * _dl_find_object. Answering "not found" is worse than not answering at all: the unwinder
 * takes it as fact, finds no frame description entry, and aborts inside uw_init_context_1
 * the moment an escape starts. A stub returning -1 is what produced exactly that here, so
 * this answers for real.
 *
 * There is one object to report, the trusted application itself, and its load address is
 * not available from a linker symbol: the kit's linker script does not define
 * __ehdr_start. The trusted-application library knows, because it loaded the module, and
 * exposes it through dl_iterate_phdr. Walking that finds the covering object's range and
 * its PT_GNU_EH_FRAME, which is everything the unwinder needs.
 */
/* glibc's struct dl_find_object, laid out as the unwinder expects it. */
struct jmpxx_dl_find_object {
  unsigned long long dlfo_flags;
  void *dlfo_map_start;
  void *dlfo_map_end;
  void *dlfo_link_map;
  void *dlfo_eh_frame;
  unsigned long long dlfo_reserved[7];
};

/* What the walk below is looking for and what it found. */
struct jmpxx_probe {
  const unsigned char *want;
  const unsigned char *start;
  const unsigned char *end;
  const void *eh_frame;
  int found;
};

static int jmpxx_cover(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size;
  struct jmpxx_probe *p = data;
  const unsigned char *base = (const unsigned char *)(uintptr_t)info->dlpi_addr;
  const unsigned char *lo = NULL;
  const unsigned char *hi = NULL;
  const void *eh_frame = NULL;
  for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
    const Elf_Phdr *ph = &info->dlpi_phdr[i];
    if (ph->p_type == PT_LOAD) {
      const unsigned char *s = base + ph->p_vaddr;
      const unsigned char *e = s + ph->p_memsz;
      if (!lo || s < lo) lo = s;
      if (!hi || e > hi) hi = e;
    } else if (ph->p_type == PT_GNU_EH_FRAME) {
      eh_frame = base + ph->p_vaddr;
    }
  }
  if (!lo || p->want < lo || p->want >= hi) return 0;  /* keep looking */
  p->start = lo;
  p->end = hi;
  p->eh_frame = eh_frame;
  p->found = 1;
  return 1;  /* stop */
}

int _dl_find_object(void *address, struct jmpxx_dl_find_object *result);

int _dl_find_object(void *address, struct jmpxx_dl_find_object *result) {
  struct jmpxx_probe probe = {address, NULL, NULL, NULL, 0};
  dl_iterate_phdr(jmpxx_cover, &probe);
  if (!probe.found || !probe.eh_frame) return -1;
  if (result) {
    result->dlfo_flags = 0;
    result->dlfo_map_start = (void *)probe.start;
    result->dlfo_map_end = (void *)probe.end;
    result->dlfo_link_map = NULL;
    result->dlfo_eh_frame = (void *)probe.eh_frame;
    for (unsigned i = 0; i < 7; ++i) result->dlfo_reserved[i] = 0;
  }
  return 0;
}

/* The enumeration the unwinder falls back to is libutee's own dl_iterate_phdr, which the
 * trusted-application library already provides, so it is deliberately not defined here. */

char *secure_getenv(const char *name) {
  (void)name;
  return NULL;
}

unsigned long __isoc23_strtoul(const char *s, char **end, int base) {
  return strtoul(s, end, base);
}
