#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define FUNCONF_USE_DEBUGPRINTF  1
#define FUNCONF_USE_USBPRINTF    0
// HPE (QingKe V4 hardware prologue/epilogue): on an interrupt the core
// saves the 16 registers a C function may change (ra, t0-t2, a0-a7, t3-t6)
// in its own register stack, and mret restores them. ch32fun's startup
// then sets INTSYSCR (0x804) to 0x0b: HPE and nesting on (standby.c
// writes it again after a wake). The CH643 nests 2 levels at most and HPE
// stacks 2 levels (CH643RM 7.1.1), so nesting can never overflow HPE.
// Today nothing preempts anyway: every interrupt has the same (reset)
// priority.
#define FUNCONF_ENABLE_HPE       1
// No fault dump over SWIO (it saves 172 B of flash). Set it to 1 to debug
// a crash: the fault handler then prints MEPC, MSTATUS, MTVAL and MCAUSE
// (minichlink -T); see CONTRIBUTING.md
#define FUNCONF_DEBUG_HARDFAULT  0
#define FUNCONF_SYSTICK_USE_HCLK 1
#define FUNCONF_USE_HSI          1
#define FUNCONF_USE_5V_VDD       1

// An interrupt handler: IRQ_HANDLER(TIM2_IRQHandler) { ... }
// With HPE the handler itself is "call body; mret" (naked, 8 bytes) and
// the body a normal C function: it saves only the s registers it uses,
// HPE did the rest. GCC has no attribute for this (WCH's own GCC has
// interrupt("WCH-Interrupt-fast")). Never write C in a naked function
// itself: it gets no stack frame and does not save s registers (that is
// what is wrong with ch32fun's own FUSB_USE_HPE, see usb_config.h).
#if FUNCONF_ENABLE_HPE
#define IRQ_HANDLER(name)                                                         \
    static void name##_body(void);                                                \
    void name(void) __attribute__((naked));                                       \
    void name(void) { __asm__ volatile("call %0\n\tmret" : : "i"(name##_body)); } \
    static void name##_body(void)
#else
#define IRQ_HANDLER(name)                    \
    void name(void) __attribute__((interrupt)); \
    void name(void)
#endif

#endif
