#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define FUNCONF_USE_DEBUGPRINTF  1
#define FUNCONF_USE_USBPRINTF    0
#define FUNCONF_ENABLE_HPE       0
// No fault dump over SWIO (it saves 172 B of flash). Set it to 1 to debug
// a crash: the fault handler then prints MEPC, MSTATUS, MTVAL and MCAUSE
// (minichlink -T); see CONTRIBUTING.md
#define FUNCONF_DEBUG_HARDFAULT  0
#define FUNCONF_SYSTICK_USE_HCLK 1
#define FUNCONF_USE_HSI          1
#define FUNCONF_USE_5V_VDD       1

#endif
