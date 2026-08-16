// SPDX-License-Identifier: MIT
// Minimal Cortex-M3 board support: vector table, reset, semihosting output, and the
// newlib syscall stubs a freestanding C++ program with exceptions still needs.
#include <cstdint>
#include <cstddef>
extern "C" {
extern std::uint32_t __stack_top, __data_start, __data_end, __data_load, __bss_start, __bss_end, __heap_start;
int main();

// Semihosting: the emulator implements these calls, which is how a board with no OS
// writes to the host console and exits.
static inline int semihost(int op, void* arg) {
  register int r0 __asm__("r0") = op; register void* r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}
void bm_write(const char* s) { semihost(0x04, (void*)s); }
[[noreturn]] void bm_exit(int code) {
  std::uint32_t a[2] = {0x20026, (std::uint32_t)code};  // ADP_Stopped_ApplicationExit
  semihost(0x20, a); for(;;){}
}

void Reset_Handler() {
  std::uint32_t *src=&__data_load,*dst=&__data_start; while(dst<&__data_end)*dst++=*src++;
  for (std::uint32_t* b=&__bss_start; b<&__bss_end; ++b) *b=0;
  bm_exit(main());
}
void Default_Handler() { bm_write("FAULT\n"); bm_exit(1); }

__attribute__((section(".isr_vector"), used))
void* const vectors[] = {
  (void*)&__stack_top, (void*)Reset_Handler,
  (void*)Default_Handler,(void*)Default_Handler,(void*)Default_Handler,
  (void*)Default_Handler,(void*)Default_Handler,
};

// newlib syscall stubs. sbrk backs the exception runtime's allocations.
void* _sbrk(std::ptrdiff_t n){ static char* brk=(char*)&__heap_start; char* p=brk; brk+=n; return p; }
int _write(int, char* buf, int len){ static char line[256]; int n=len<255?len:255;
  for(int i=0;i<n;++i) line[i]=buf[i]; line[n]=0; bm_write(line); return len; }
int _close(int){return -1;} int _lseek(int,int,int){return 0;} int _read(int,char*,int){return 0;}
int _fstat(int,void*){return 0;} int _isatty(int){return 1;} int _getpid(){return 1;}
int _kill(int,int){return -1;} void _exit(int c){ bm_exit(c); }
// The EABI reads thread-local storage through this hook. One fixed block is right for a
// single-threaded board, and the arm's per-thread landing state lives there.
void* __aeabi_read_tp(){ static std::uint8_t tls[512]; return tls; }
}
