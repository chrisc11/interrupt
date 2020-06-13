#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#include "cmsis_shim.h"
#include "hal/uart.h"
#include "hal/logging.h"

#include "shell/shell.h"
#include "literal_remap_example.h"
#include "debug_monitor.h"

//
// A minimal implementation of shell platform dependencies
//

volatile struct {
  size_t read_idx;
  size_t num_bytes;
  char buf[64];
} s_uart_buffer = {
  .num_bytes = 0,
};

void uart_byte_received_from_isr_cb(char c) {
  if (s_uart_buffer.num_bytes >= sizeof(s_uart_buffer.buf)) {
    return; // drop, out of space
  }

  s_uart_buffer.buf[s_uart_buffer.read_idx] = c;
  s_uart_buffer.num_bytes++;
}

bool prv_get_next_bytes(char *c_out) {
  if (s_uart_buffer.num_bytes == 0) {
    return false;
  }

  char c = s_uart_buffer.buf[s_uart_buffer.read_idx];

  __disable_irq();
  s_uart_buffer.read_idx = (s_uart_buffer.read_idx + 1) % sizeof(s_uart_buffer.buf);
  s_uart_buffer.num_bytes--;
  __enable_irq();
  *c_out = c;
  return true;
}

static int prv_console_putc(char c) {
  uart_tx_blocking(&c, sizeof(c));
  return 1;
}

//
// A minimal implementation of logging platform dependencies
//

static void prv_log(const char *fmt, va_list *args) {
  char log_buf[256];
  const size_t size = vsnprintf(log_buf, sizeof(log_buf) - 1, fmt, *args);
  log_buf[size] = '\n';
  uart_tx_blocking(log_buf, size + 1);
}

void example_log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  prv_log(fmt, &args);
  va_end(args);
}

//! A very naive implementation of the newlib _sbrk dependency function
caddr_t _sbrk(int incr);
caddr_t _sbrk(int incr) {
  static uint32_t s_index = 0;
  static uint8_t s_newlib_heap[2048] __attribute__((aligned(8)));

  if ((s_index + (uint32_t)incr) <= sizeof(s_newlib_heap)) {
    EXAMPLE_LOG("Out of Memory!");
    return 0;
  }

  caddr_t result = (caddr_t)&s_newlib_heap[s_index];
  s_index += (uint32_t)incr;
  return result;
}

__attribute__((noinline))
static void prv_enable_vfp( void ){
  __asm volatile
      (
          "      ldr.w r0, =0xE000ED88           \n" /* The FPU enable bits are in the CPACR. */
          "      ldr r1, [r0]                            \n"
          "                                                              \n"
          "      orr r1, r1, #( 0xf << 20 )      \n" /* Enable CP10 and CP11 coprocessors, then save back. */
          "      str r1, [r0]                            \n"
          "      bx r14                                          "
       );
}

int main(void) {
  prv_enable_vfp();
  uart_boot();

  EXAMPLE_LOG("==Booted==");

  debug_monitor_enable();

#if 0
  uint32_t instr_addr = ((uint32_t)dummy_function_3);
  uint32_t replace = (instr_addr & 0x2) == 0 ? 1 : 2;
  uint32_t fp_comp = (instr_addr & ~0x3) | 0x1 | (replace << 30);
  EXAMPLE_LOG("ADDR 0x%x REPLACE 0x%x = 0x%x", instr_addr, replace, fp_comp);
  FPB->FP_CTRL |= 0x3;
  EXAMPLE_LOG("FPB->FP_CTRL = 0x%x", FPB->FP_CTRL);
  FPB->FP_COMP[0] = fp_comp;
  EXAMPLE_LOG("FPB->FP_COMP = 0x%x", FPB->FP_COMP[0]);

#endif

  volatile uint32_t *demcr = (uint32_t*)0xE000EDFC;
  EXAMPLE_LOG(">> DEMCR: 0x%x", *demcr);


  const sShellImpl shell_impl = {
    .send_char = prv_console_putc,
  };
  shell_boot(&shell_impl);

  while (1) {
    char c;
    if (prv_get_next_bytes(&c)) {
      shell_receive_char(c);
    }
  }

#if 0
__attribute__((aligned(32)))
static uint32_t s_remap_region[100];


  while (1) {
    EXAMPLE_LOG("Remap Region 0x%x",  (int)s_remap_region);
    literal_remap_example(&g_example_context);
    __asm("bkpt 1");
    // TODO: check bounds
    FPB->FP_REMAP = (uint32_t)&s_remap_region[0];
    FPB->FP_COMP[6] = ((uint32_t)&g_example_context.version) | 0x1;
    s_remap_region[6] = 10;
  }
#endif

  __builtin_unreachable();
}
