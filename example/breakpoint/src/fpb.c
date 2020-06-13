#include "fpb.h"

#include "hal/logging.h"

typedef struct {
  volatile uint32_t FP_CTRL;
  volatile uint32_t FP_REMAP;
  // Maximum Possible FP_COMP registers.
  //
  // Actual Number Implemented determined by FP_CTRL
  volatile uint32_t FP_COMP[142];
} sFpbUnit;

static sFpbUnit *const FPB = (sFpbUnit *)0xE0002000;

void fpb_disable(void) {
  FPB->FP_CTRL = (FPB->FP_CTRL & ~0x3) | 0x2;
}

void fpb_enable(void) {
  FPB->FP_CTRL |= 0x3;
}


  // The instruction address comparators start at FP_COMP0. This means the last instruction address
  // comparator is FP_COMPn, where n = (NUM_CODE-1). The maximum number of instruction address
  // comparators is 127.

  // The literal address comparators start at FP_COMPm, where m=NUM_CODE. This means the last
  // literal address comparator is at FP_COMPp, where p=(NUM_CODE+NUM_LIT-1). The maximum number of
  // literal address comparators is 15.

void fpb_get_config(sFpbConfig *config) {
  uint32_t fp_ctrl = FPB->FP_CTRL;

  const uint32_t enabled = fp_ctrl & 0x1;
  const uint32_t revision = (fp_ctrl >> 28) & 0xF;
  const uint32_t num_code =
      (((fp_ctrl >> 12) & 0x7) << 4) | ((fp_ctrl >> 4) & 0xF);
  const uint32_t num_lit = (fp_ctrl >> 8) & 0xF;

  *config = (sFpbConfig) {
    .enabled = enabled != 0,
    .revision = revision,
    .num_code_comparators = num_code,
    .num_literal_comparators = num_lit,
  };
}

bool fpb_set_breakpoint(size_t comp_id, uint32_t instr_addr) {
  sFpbConfig config;
  fpb_get_config(&config);
  if (config.revision != 0) {
    EXAMPLE_LOG("Revision %d Parsing Not Supported", config.revision);
    return false;
  }

  const size_t num_comps = config.num_code_comparators;
  if (comp_id >= num_comps) {
    EXAMPLE_LOG("Instruction Comparator %d Not Implemented", num_comps);
    return false;
  }

  if (instr_addr >= 0x20000000) {
    EXAMPLE_LOG("Address 0x%x is not in code region", instr_addr);
    return false;
  }

  if (!config.enabled) {
    EXAMPLE_LOG("Enabling FPB.");
    fpb_enable();
  }


  const uint32_t replace = (instr_addr & 0x2) == 0 ? 1 : 2;
  const uint32_t fp_comp = (instr_addr & ~0x3) | 0x1 | (replace << 30);
  FPB->FP_COMP[comp_id] = fp_comp;
  return true;
}

bool fpb_get_comp_config(size_t comp_id, sFpbCompConfig *comp_config) {
  sFpbConfig config;
  fpb_get_config(&config);
  if (config.revision != 0) {
    EXAMPLE_LOG("Revision %d Parsing Not Supported", config.revision);
    return false;
  }

  const size_t num_comps = config.num_code_comparators + config.num_literal_comparators;
  if (comp_id >= num_comps) {
    EXAMPLE_LOG("Comparator %d Not Implemented", num_comps);
    return false;
  }

  uint32_t fp_comp = FPB->FP_COMP[comp_id];
  bool enabled = fp_comp & 0x1;
  uint32_t replace = fp_comp >> 30;

  uint32_t address = fp_comp & 0x1FFFFFFC;
  if (replace == 0x2) {
    address |= 0x2;
  }

  *comp_config = (sFpbCompConfig) {
    .enabled = enabled,
    .replace = replace,
    .address = address,
  };
  return true;
}
