---
title: "What's in a Breakpoint?"
description: ""
tag: [cortex-m]
author: chris
---

Have you ever wondered what actually happens when you enable a breakpoint and how it works? Or at the very least, have you ever gotten upset when a breakpoint doesn't work?

Having a working understanding of what is happening in this area can help you work through issues you may encounter in your debug setup or help you implement your own or improve breakpoint implementations in a debugger!

<!-- excerpt start -->

In this article we will discuss how hardware and software breakpoints work. We will then examine how they are leveraged by a conventional debugger, GDB. We will explore how to configure hardware breakpoints on ARM Cortex-M MCUs using the Flash Patch and Breakpoint Unit (_FPB_). And finally we will take advantage of an underutilized feature of the ARM Cortex-M chipset, the DebugMonitor Exception, to implement breakpoint functionality on a NRF52 over UART without any debugger attached!

<!-- excerpt end -->

> Note: While the focus of the article will be using Breakpoints with ARM Cortex-M embedded devices, the
> general overview and walkthrough with GDB can be applied to any architecture
> supported by the toolchain.

_Like Interrupt? [Subscribe](http://eepurl.com/gpRedv) to get our latest
posts straight to your mailbox_

{:.no_toc}

## Table of Contents

<!-- prettier-ignore -->
* auto-gen TOC:
{:toc}

## Basic Terminology

Before we get started,

### Hardware Breakpoint

As the name implies, Hardware Breakpoints are provided by the physical MCU. They are comparators which can be configured via peripheral registers. When an instruction is fetched by the MCU and its value matches one of the comparators a debug event will be generated which halts the core. There's usually a small, fixed number of hardware breakpoints available for any given MCU. For ARM Cortex-M's this unit is known as the **FPB** ("Flash Patch and Breakpoint Unit").

### Software Breakpoint

Software Breakpoints on the other hand are implemented by your debugger. They work by replacing the actual code you are trying to execute with an instruction which triggers a halt in some manner (either by using a dedicated instruction to trigger a breakpoint or when that is not supported by generating an exception). The advantage here is by utilizing software breakpoints you can basically have an unlimited number of them. The disadvantage is now you have to manage all the code patching.

## Project Setup

In the following sections will walk through an example

- a nRF52840-DK[^1] (ARM Cortex-M4F) as our development board
- SEGGER JLinkGDBServer[^2] as our GDB Server (V6.80b)
- GCC 9.3.1 / GNU Arm Embedded Toolchain as our compiler[^3]
- GNU make as our build system

The app itself is a bare-metal application which makes use of the [demo cli shell]({% post_url 2020-06-09-firmware-shell %}) we built up in a previous post to test out functionality related to breakpoints.

### GDB Breakpoint Handling

Let's start by taking a look at how breakpoints are handled with GDB.

> A full discussion of debug interfaces is outside the scope of this article but more details can be found in [our post on the topic]({% post_url 2019-08-06-a-deep-dive-into-arm-cortex-m-debug-interfaces %}).

At a high level, `gdb` (the "client") interfaces with the embedded MCU via the `gdbserver` (in our case JLinkGDBServer). The protocol talked between the gdb client and the gdb server is known as "GDB Remote Serial Protocol"[^4].

If we take a look at the document we can find the commands that are used to set and clear breakpoints. They are the ['z' packets](https://sourceware.org/gdb/onlinedocs/gdb/Packets.html#index-z-packet) which takes the form:

`‘Z/z type,addr,kind’`

GDB has different commands which are issued to install a hardware or software breakpoint.

- Insert (‘Z0’) or remove (‘z0’) a software breakpoint at address addr of type kind.
- Insert (‘Z1’) or remove (‘z1’) a hardware breakpoint at address addr.

Conveniently, `gdb` exposes a way to dump all the transactions sent between the client and server via a debug command, `set debug remote 1`. Let's enable that and take a look at what happens when we try to install a breakpoint!

To facilitate breakpoint testing, I've created `dummy_function_1`..`dummy_function15` in the example app which we can try to install breakpoints on.

```
$ JLinkGDBServer  -if swd -device nRF52840_xxAA  -nogui

```

```
$ git clone https://github.com/memfault/interrupt.git
$ cd examples/breakpoint

# Build
$ make
# Flash
$ arm-none-eabi-gdb-py --eval-command="target remote localhost:2331"  --ex="mon reset" --ex="load" --ex="mon reset"  --se=build/nrf52.elf
(gdb)
```

```
(gdb) set debug remote 1
(gdb)
(gdb) p/x &dummy_function_1
$3 = 0x428
(gdb) break dummy_function_1
Sending packet: $m80,4#35...Packet received: dff80c00
Sending packet: $m400,40#91...Packet received: 10b5074cd4f8103143b10023c4f81031044b1878fff75cfe0123236010bd00bf002000400012002001490248fff77ebe6e7000005470000001490248fff776be
Sending packet: $m430,4#64...Packet received: 6e700000
Sending packet: $m430,4#64...Packet received: 6e700000
Sending packet: $m428,2#69...Packet received: 0149
Breakpoint 1 at 0x428: file /Users/chrisc/dev/interrupt/example/breakpoint/src/stub_functions.c, line 4.
```

We see `m` packets[^5] being sent which are requests to read memory at the gievn address `‘m addr,length’`

Interesting, so we don't see any requests related to breakpoints!

Let's see what happens when we continue:

```
(gdb) continue
Continuing.
Sending packet: $Z0,428,2#b2...Packet received: OK
Packet Z0 (software-breakpoint) is supported
Sending packet: $vCont?#49...Packet received:
Packet vCont (verbose-resume) is NOT supported
Sending packet: $Hc0#db...Packet received: OK
Sending packet: $c#63...Packet received: T05hwbreak:;thread:0000DEAD;
Sending packet: $g#67...Packet received: 2032002027000000010000000020004000000000000000000000000000000000000000000000000000000000000000000000000060330020bb0200002804000000000e61
Sending packet: $qfThreadInfo#bb...Packet received: m0000dead
Sending packet: $qsThreadInfo#c8...Packet received: l
Sending packet: $z0,428,2#d2...Packet received: OK

Breakpoint 1, Sending packet: $m428,4#6b...Packet received: 01490248
dummy_function_1 () at /Users/chrisc/dev/interrupt/example/breakpoint/src/stub_functions.c:4
4     EXAMPLE_LOG("stub function '%s' called", __func__);
```

Breakpoints are registered on resume and removed on halt to decrease the possiblity of leaving the system in a bad state if the system is disconnected on halt.

### Flash Patch & Breakpoint Unit (FPB)

For any ARM Cortex-M MCU, hardware breakpoint functionality is provided via the FPB. The FPB allows hardware breakpoints to be configured as well as flash memory "patching". The patch feature allows accesses to flash memory to be remapped into RAM. In the past this was a pretty popular way for vendors to ship updates to stacks sitting in ROM. These days with more and more devices being fully firmware updateable its a little less common.

> NOTE. These features only work on the Code Address Space, 0x00000000-0x1FFFFFFF

The Unit is Comprised of 3 types of registers

- `FP_CTRL` -
- `FP_REMAP` -
- `FP_COMP[N]` -

If we want to read it in code we can define a simple C structure as follows:

```c
typedef struct {
  volatile uint32_t FP_CTRL;
  volatile uint32_t FP_REMAP;
  // Maximum Possible FP_COMP registers.
  //
  // Actual Number Implemented determined by FP_CTRL
  volatile uint32_t FP_COMP[142];
} sFpbUnit;

static sFpbUnit *const FPB = (sFpbUnit *)0xE0002000;
```

#### Flash Patch Control Register, FP_CTRL, 0xE0002000

![](/img/breakpoint/fp-ctrl.png)

This register bank is part of all Cortex-M architectures (ARMv6-M, ARMv7-M, & ARMv8-M) architectures and allows you to determine how many hardware breakpoints are available as well as enable the feature.

Notably,

- Number of Code Hardware Breakpoints = (FP_CTRL[14:12] << 4) | FP_CTRL[7:4]. So for the NRF52 we have:

```
(gdb) p/x (*(uint32_t*)0xE0002000>>4 & 0xF) | ((*(uint32_t*)0xE0002000>>12 & 0x7) << 4)
```

- Number of Literal Comparators:

#### Flash Patch Comparator register, FP_COMPn, 0xE0002008- 0xE0002008+4n

##### Revision 1 Layout

![](/img/breakpoint/fp-comp-rev1.png)

##### Revision 2 Layout

![](/img/breakpoint/fp-comp-rev2-dcba.png)

![](/img/breakpoint/fp-comp-rev2-bpaddr.png)

## Breakpoints without GDB!

ARM Cortex-M Cores support two main modes of debug:

- `halting` debug - This is the typical configuration you use with GDB. In this mode, the core is halted while debugging. This mode requires access to the **Debug Port** via JTAG or SWD.
- `monitor` mode debug - In this mode, a debug event generates a built in exception known as the DebugMonitor exception instead of halting the core. This enabled debug when a SWD/JTAG connection may not be feasible (i.e device just exposing a UART) and allows for debug whiel a system continues to be run. This can be helpful in scenarios where halting the core will cause timing sensitive subsystems to fail (such as the Bluetooth Radio on the NRF52).

There's a few cool projects that make use of Monitor Mode Debugging:

- [SEGGERs Monitor Mode Debugging](https://www.segger.com/products/debug-probes/j-link/technology/monitor-mode-debugging/)
- [The Monitor For Remote Inspection Project on GitHub](https://github.com/adamgreen/mri)

In the following sections we'll take a look at how to enable monitor mode debugging and implement a very minimal breakpoint handler!

### Enabling DebugMonitor

#### Debug Halting Control and Status Register (DHCSR), 0xE000EDF0

![](/img/breakpoint/dhcsr.png)

Monitor Mode Debug only works if Halting Mode debug is disabled. Notably, the `C_DEBUGEN` setting above must read zero. This bit can _only_ be set via a debugger and is _only_ reset otherwise when a full Power-On-Reset (POR) occurs. If you are trying to use Monitor Mode Debug, make sure to check this setting:

```c
  volatile uint32_t *dhcsr = (uint32_t*)0xE000EDF0;
  if ((*dhcsr & 0x1) != 0) {
    // Halting debugging is enabled, DebugMonitor settings will not have any effect
    return;
  }
  // set up monitor mode
```

```
set *(uint32_t*)0xE000EDF0=(0xA05F<<16)
```

#### Debug Exception and Monitor Control Register (DEMCR), 0xE000EDFC

The core configuration for the DebugMonitor exception is controlled in the DEMCR:

![](/img/breakpoint/demcr.png)

where:

- `MON_EN` - Controls whether the DebugMonitor exception is enabled or not
- `MON_STEP` - Can be toggled from the DebugMonitor exception to enable hardware single-stepping. Basically when set, the core will execute a single instruction and then return to the DebugMonitor exception.
- `VC_HARDERR`, etc - Controls whether or not a debug trap occurs automatically when various types of exceptions take place. These _only_ take effect when using `halting` debug mode.

> Note: The DebugMonitor Exception will only be triggered for debug events when the group priority of the exception is greater than the current execution priority. This can be a useful feature to guarantee that certain high priority operations (i.e BT Radio Scheduling) continue to run while using the debug monitor. An important consequence however is it does mean any interrupts above this priority will not be debuggable using monitor mode debug. Configuring the DebugMonitor exception priority will require updating SHPR3. More details can be found in our post about Cortex-M Exceptions [here]({% post_url 2019-09-04-arm-cortex-m-exceptions-and-nvic %}#system-handler-priority-register-shpr1-shpr3---0xe000ed18---0xe000ed20)

#### Debug Fault Status Register, DFSR, 0xE000ED30

To determine the debug event which took place we can take a look at the DFSR:

![](/img/breakpoint/dfsr.png)

Notably, when the DebugMonitor exception is active we have:

- `BKPT` Indicates one or more breakpoint event took place (either via the FPB or an actual instruction)
- `HALTED` Indicates the core was halted due to a MON_STEP request

> NOTE: DFSR bits are sticky and you have to write 1 to the value to clear them

#### A minimal DebugMonitor Handler

We can use the same handler we put together in [this post]({% post_url 2019-11-20-cortex-m-fault-debug %}#halting--determing-core-register-state) to dump some information when a breakpoint is hit:

```
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
  }
}

void DebugMonitor_Exception(void) {
  // pick up the stack active prior to exception entry
  __asm volatile(
      "tst lr, #4 \n"
      "ite eq \n"
      "mrseq r0, msp \n"
      "mrsne r0, psp \n"
      "b debug_monitor_handler_c \n");
}
```

##### Triggering a breakpoint

Let's see what we get:

```
shell> debug_mon_en
Monitor Mode Debug Enabled!
shell> bkpt
DebugMonitor Exception
DEMCR: 0x00010000
DFSR: 0x00000002
Register Dump
 r0  =0x00000001
 r1  =0x20003318
 r2  =0x0074706b
 r3  =0x0000056d
 r12 =0xffffffff
 lr  =0x000004e3
 pc  =0x0000056c
 xpsr=0x61070000
```

As expected we see bit 1 in DFSR is set indicating a breakpoint took place.

#### Installing Breakpoints with the FPB

```c
bool fpb_set_breakpoint(size_t comp_id, uint32_t instr_addr) {
   if (instr_addr >= 0x20000000) {
   // for revision 1 only breakpoints in code can be installed :/
    return false;
    }
    //
    FPB->FP_CTRL |= 0x3;

  const uint32_t replace = (instr_addr & 0x2) == 0 ? 1 : 2;
  const uint32_t fp_comp = (instr_addr & ~0x3) | 0x1 | (replace << 30);
  FPB->FP_COMP[comp_id] = fp_comp;
  return true;
}
```

#### Naive DebugMonitor Breakpoint Handler

![](/img/breakpoint/bkpt-instruction-cortex-m.png)

```c
void debug_monitor_handler_c(sContextStateFrame *frame) {
// ...


```

## Final Thoughts

See anything you'd like to change? Submit a pull request or open an issue at [Github](https://github.com/memfault/interrupt)

{:.no_toc}

## References

[^1]: [nRF52840 Development Kit](https://www.nordicsemi.com/Software-and-Tools/Development-Kits/nRF52840-DK)
[^2]: [JLinkGDBServer](https://www.segger.com/products/debug-probes/j-link/tools/j-link-gdb-server/about-j-link-gdb-server/)
[^3]: [GNU ARM Embedded toolchain for download](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads)
[^4]: [Official GDB Remote Serial Protocol Docs](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html) & [Good Accompanying Docs](https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.html)
[^5]: [m-packet](https://sourceware.org/gdb/onlinedocs/gdb/Packets.html#index-m-packet)
[^6]: [GDB Internals Breakpoint Handling](https://sourceware.org/gdb/wiki/Internals/Breakpoint%20Handling)
[^7]: https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.html
