#include <stdbool.h>

#include "cmsis_shim.h"
#include "hal/uart.h"
#include "hal/logging.h"
#include "stub_functions.h"
#include "literal_remap_example.h"
#include "debug_monitor.h"
#include "fpb.h"


typedef struct __attribute__((packed)) ContextStateFrame {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t return_address;
  uint32_t xpsr;
} sContextStateFrame;

static bool prv_should_continue(void) {
  extern bool prv_get_next_bytes(char *c_out);
  while (1) {
    char c;
    if (!prv_get_next_bytes(&c)) {
      return false;
    }

    EXAMPLE_LOG("Got char '%c'!\n", c);
    if (c == 'c')  {
      return true;
    }
  }
}

void debug_monitor_handler_c(sContextStateFrame *frame) {
  static uint32_t s_last_pc = 0;
  volatile uint32_t *demcr = (uint32_t*)0xE000EDFC;

  volatile uint32_t *dfsr = (uint32_t*)0xE000ED30;
  const uint32_t dfsr_bkpt_evt_bitmask = 0x2;
  const uint32_t dfsr_halt_evt_bitmask = 0x1;
  const bool is_bkpt_dbg_evt = (*dfsr & dfsr_bkpt_evt_bitmask);
  const bool is_halt_dbg_evt = (*dfsr & dfsr_halt_evt_bitmask);

  if (frame->return_address != s_last_pc) {
    EXAMPLE_LOG("DebugMonitor Exception");

    EXAMPLE_LOG("DEMCR: 0x%08x", *demcr);
    EXAMPLE_LOG("DFSR:  0x%08x (bkpt=%d, halt=%d)", *dfsr,
                (int)is_bkpt_dbg_evt, (int)is_halt_dbg_evt);

    EXAMPLE_LOG("Register Dump");
    EXAMPLE_LOG(" r0  =0x%08x", frame->r0);
    EXAMPLE_LOG(" r1  =0x%08x", frame->r1);
    EXAMPLE_LOG(" r2  =0x%08x", frame->r2);
    EXAMPLE_LOG(" r3  =0x%08x", frame->r3);
    EXAMPLE_LOG(" r12 =0x%08x", frame->r12);
    EXAMPLE_LOG(" lr  =0x%08x", frame->lr);
    EXAMPLE_LOG(" pc  =0x%08x", frame->return_address);
    EXAMPLE_LOG(" xpsr=0x%08x", frame->xpsr);
    s_last_pc = frame->return_address;

    if (is_bkpt_dbg_evt)  {
      EXAMPLE_LOG("Breakpoint Detected, Awaiting 'c'");
    }
  }


  const uint32_t demcr_single_step_mask = (1 << 18);

  if (is_bkpt_dbg_evt) {
    // wait for continuation request
    if (!prv_should_continue()) {
      return; // only handle breakpoint events for now
    }

    // we got a continuation request, now what!

    // breakpoint instructions are two bytes and
    const uint16_t instruction = *(uint16_t*)frame->return_address;
    if ((instruction & 0xff00) == 0xbe00) {
      // advance past breakpoint
      frame->return_address += sizeof(instruction);
    } else {
      // It's a FPB generated breakpoint

      // disable FPB
      fpb_disable();
      EXAMPLE_LOG("Single-Stepping over FPB at 0x%x", frame->return_address);
      *demcr |= (demcr_single_step_mask);
    }

    // We have serviced the breakpoint event so clear mask
    *dfsr = dfsr_bkpt_evt_bitmask;
  } else if (is_halt_dbg_evt) {
    EXAMPLE_LOG("Disabling Single Step and Re-enabling FPB");
    fpb_enable();
    *demcr &= ~(demcr_single_step_mask);

    // We have serviced the single step event so clear mask
    *dfsr = dfsr_halt_evt_bitmask;
  }
}

void DebugMonitor_Exception(void) {
  __asm volatile(
      "tst lr, #4 \n"
      "ite eq \n"
      "mrseq r0, msp \n"
      "mrsne r0, psp \n"
      "b debug_monitor_handler_c \n");
}

static void prv_enable(bool do_enable) {
  volatile uint32_t *demcr = (uint32_t*)0xE000EDFC;
  const uint32_t mon_en_bit = 16;
  if (do_enable) {
    *demcr |= 1<<mon_en_bit;
  } else {
    *demcr &= ~(1<<mon_en_bit);
  }
}

static bool prv_halting_debug_enabled(void) {
  volatile uint32_t *dhcsr = (uint32_t *)0xE000EDF0;
  return (((*dhcsr) & 0x1) != 0);
}

bool debug_monitor_enable(void) {
  if (prv_halting_debug_enabled()) {
    EXAMPLE_LOG("Halting Debug Enabled - Can't Enable Monitor Mode Debug");
    return false;
  }
  prv_enable(true);


  // Priority for DebugMonitor Exception is bits[7:0]. We will use the lowest
  // priority so other ISRs can fire while in the DebugMonitor Interrupt
  volatile uint32_t *shpr3 = (uint32_t *)0xE000ED20;
  *shpr3 = 0xff;

  EXAMPLE_LOG("Monitor Mode Debug Enabled!");
  return true;
}

bool debug_monitor_disable(void) {
  prv_enable(false);
  return true;
}
