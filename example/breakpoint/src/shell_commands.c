#include "shell/shell.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>

#include "fpb.h"
#include "debug_monitor.h"
#include "stub_functions.h"
#include "hal/logging.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

static int prv_dump_fpb_config(int argc, char *argv[]) {
  sFpbConfig fpb_config;
  fpb_get_config(&fpb_config);
  EXAMPLE_LOG("FPB Revision: %d: Enabled: %d. Hardware Breakpoints: %d. Literal Comparators: %d",
              fpb_config.revision,
              (int)fpb_config.enabled,
              (int)fpb_config.num_code_comparators,
              (int)fpb_config.num_literal_comparators);

  const size_t num_comps = fpb_config.num_code_comparators +
      fpb_config.num_literal_comparators;

  for (size_t i = 0; i < num_comps; i++) {
    sFpbCompConfig comp_config;
    if (!fpb_get_comp_config(i, &comp_config)) {
      continue;
    }

    if (!comp_config.enabled) {
      EXAMPLE_LOG("  FP_COMP[%d] Disabled", i);
      continue;
    }

    EXAMPLE_LOG("  FP_COMP[%d] Replace: %d, Address 0x%x", i,
                comp_config.replace, comp_config.address);
  }

  return 0;
}

static void (*s_stub_funcs[])(void) = {
  dummy_function_1,
  dummy_function_2,
  dummy_function_3,
  dummy_function_4,
  dummy_function_5,
  dummy_function_6,
  dummy_function_7,
  dummy_function_8,
  dummy_function_9,
  dummy_function_ram,
};

static int prv_call_dummy_funcs(int argc, char *argv[]) {
  for (size_t i = 0; i < ARRAY_SIZE(s_stub_funcs); i++) {
    s_stub_funcs[i]();
  }
  return 0;
}

static int prv_dump_dummy_funcs(int argc, char *argv[]) {
  for (size_t i = 0; i < ARRAY_SIZE(s_stub_funcs); i++) {
    // physical address is function address with thumb bit removed
    volatile uint32_t *addr = (uint32_t *)(((uint32_t)s_stub_funcs[i]) & ~0x1);
    EXAMPLE_LOG("Instruction at 0x%x = 0x%x", addr, *addr);
  }

  return 0;
}

static int prv_fpb_set_breakpoint(int argc, char *argv[]) {
  if (argc < 3) {
    EXAMPLE_LOG("Expected [Comp Id] [Address]");
    return -1;
  }

  size_t comp_id = strtoul(argv[1], NULL, 0x0);
  uint32_t addr = strtoul(argv[2], NULL, 0x0);

  bool success = fpb_set_breakpoint(comp_id, addr);
  EXAMPLE_LOG("Set breakpoint on address 0x%x in FP_COMP[%d] %s", addr,
              (int)comp_id, success ? "Succeeded" : "Failed");

  return success ? 0 : -1;
}

static int prv_debug_monitor_enable(int argc, char *argv[]) {
  debug_monitor_enable();
  return 0;
}

static int prv_issue_breakpoint(int argc, char *argv[]) {
  __asm("bkpt 1");
  return 0;
}

static const sShellCommand s_shell_commands[] = {
  {"bkpt", prv_issue_breakpoint, "Issue a Breakpoint Exception" },
  {"debug_mon_en", prv_debug_monitor_enable, "Enable Monitor Debug Mode" },
  {"fpb_dump", prv_dump_fpb_config, "Dump Active FPB Settings"},
  {"fpb_set_breakpoint", prv_fpb_set_breakpoint, "Set Breakpoint [Comp Id] [Address]"},
  {"call_dummy_funcs", prv_call_dummy_funcs, "Invoke dummy functions"},
  {"dump_dummy_funcs", prv_dump_dummy_funcs, "Print first instruction of each dummy function"},
  {"help", shell_help_handler, "Lists all commands"},
};

const sShellCommand *const g_shell_commands = s_shell_commands;
const size_t g_num_shell_commands = ARRAY_SIZE(s_shell_commands);
