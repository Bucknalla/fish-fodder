// Just enough to run C on QEMU's Xtensa "sim" machine: start-up, and output
// and exit through semihosting (the simcall instruction).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int simcall(int nr, int a, int b, int c) {
  register int a2 __asm__("a2") = nr;
  register int a3 __asm__("a3") = a;
  register int a4 __asm__("a4") = b;
  register int a5 __asm__("a5") = c;
  __asm__ volatile("simcall" : "+r"(a2), "+r"(a3) : "r"(a4), "r"(a5) : "memory");
  return a2;
}

static char obuf[4096];
static int on;
static void flush(void) {
  if (on) simcall(4 /* write */, 1, (int)obuf, on);
  on = 0;
}
static int sim_putc(char c, FILE *f) {
  (void)f;
  obuf[on++] = c;
  if (c == '\n' || on == (int)sizeof obuf) flush();
  return (unsigned char)c;
}
static FILE io = FDEV_SETUP_STREAM(sim_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &io, *const stderr = &io, *const stdin = &io;

void _exit(int code) {
  flush();
  simcall(1 /* exit */, code, 0, 0);
  for (;;);
}

extern char __bss_start[], __bss_end[];
int main(void);
void c_start(void) {
  memset(__bss_start, 0, __bss_end - __bss_start);
  exit(main());
}

// 64-bit division, which Ubuntu's lx106 libgcc leaves to the ESP8266's ROM.
typedef unsigned long long u64;
typedef long long s64;
u64 __udivmoddi4(u64, u64, u64 *);
u64 __udivdi3(u64 a, u64 b) { return __udivmoddi4(a, b, 0); }
u64 __umoddi3(u64 a, u64 b) {
  u64 r;
  __udivmoddi4(a, b, &r);
  return r;
}
s64 __divdi3(s64 a, s64 b) {
  u64 q = __udivmoddi4(a < 0 ? -(u64)a : (u64)a, b < 0 ? -(u64)b : (u64)b, 0);
  return (a < 0) ^ (b < 0) ? -(s64)q : (s64)q;
}
s64 __moddi3(s64 a, s64 b) {
  u64 r;
  __udivmoddi4(a < 0 ? -(u64)a : (u64)a, b < 0 ? -(u64)b : (u64)b, &r);
  return a < 0 ? -(s64)r : (s64)r;
}

__asm__(".text\n"
        ".literal_position\n"
        ".literal .Lstack, __stack_top\n"
        ".global _start\n"
        ".align 4\n"
        "_start:\n"
        "  l32r a1, .Lstack\n"
        "  call0 c_start\n");

// The sim machine starts at the reset vector, not the ELF entry point.
__asm__(".section .reset, \"ax\"\n"
        "  j 1f\n"
        "  .align 4\n"
        "2: .word _start\n"
        "1: l32r a0, 2b\n"
        "  jx a0\n"
        ".text\n");
