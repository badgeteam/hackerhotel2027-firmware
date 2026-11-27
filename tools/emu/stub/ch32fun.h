// The emulator's stand-in for ch32fun.h: just enough of the chip for the
// firmware files that the emulator builds unchanged (see tools/emu/Makefile).
// Register writes go to plain variables; the parts that matter (buttons,
// time, the USB endpoint) are wired to the emulator in hw.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define __IO volatile

typedef struct { __IO uint32_t CFGLR, CFGHR, INDR, OUTDR, BSHR, BCR, LCKR, CFGXR, BSXR; } GPIO_TypeDef;
typedef struct { __IO uint32_t ECR, PCFR1, EXTICR1, EXTICR2, CR, PCFR2, CTLR; } AFIO_TypeDef;
typedef struct { __IO uint32_t AHBPCENR, APB1PCENR, APB2PCENR; } RCC_TypeDef;
typedef struct { __IO uint8_t BASE_CTRL, UDEV_CTRL; } USBFS_TypeDef;
typedef struct { __IO uint32_t UID0, UID1, UID2; } ESG_TypeDef;

extern GPIO_TypeDef emu_gpio[3];  // A, B, C; INDR of B and C carries the buttons, USB power and the charger
extern AFIO_TypeDef emu_afio;
extern RCC_TypeDef emu_rcc;
extern USBFS_TypeDef emu_usbfs;
extern ESG_TypeDef emu_esig;
#define GPIOA (&emu_gpio[0])
#define GPIOB (&emu_gpio[1])
#define GPIOC (&emu_gpio[2])
#define AFIO (&emu_afio)
#define RCC (&emu_rcc)
#define USBFS (&emu_usbfs)
#define ESIG (&emu_esig)

// pins as ch32fun numbers them: port * 32 + pin
#define PA0 0
#define PB0 32
#define PC0 64
#define PC4 (PC0 + 4)
#define PC5 (PC0 + 5)
#define GpioOf(pin) (&emu_gpio[(pin) >> 5])
#define funDigitalRead(pin) ((int)((GpioOf(pin)->INDR >> ((pin) & 31)) & 1))
#define funPinMode(pin, mode) ((void)(pin), (void)(mode))
#define GPIO_CFGLR_IN_PUPD 0x8

#define RCC_USBFS (1u << 12)
#define RCC_IOPBEN (1u << 3)  // power.c
#define RCC_IOPCEN (1u << 4)
#define RB_UC_RESET_SIE 0x04
#define RB_UC_CLR_ALL 0x02
#define UDP_PUE_MASK 0x0C
#define UDM_PUE_MASK 0x30
#define USB_IOEN 0x80
#define USB_PHY_V33 0x40
#define USBPD_PHY_V33 0x100
#define USBFS_IRQn 0
#define NVIC_DisableIRQ(irq) ((void)(irq))
#define NVIC_EnableIRQ(irq) ((void)(irq))

// time: 1 ms = 1000 ticks, so funSysTick32() wraps after 71 minutes
#define FUNCONF_SYSTEM_CORE_CLOCK 1000000
#define DELAY_MS_TIME 1000
uint32_t funSysTick32(void);
void Delay_Ms(uint32_t ms);  // also where "interrupts" (USB, IR) get in
static inline void SystemInit(void) {}
static inline void funGpioInitAll(void) {}

// interrupts: commands from the emulator run as an "interrupt" while the
// firmware holds no lock (hw.c); the firmware's own critical sections are
// already covered by that lock
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}

// the firmware's printf goes to the emulator's log
int emu_printf(const char* fmt, ...) __attribute__((format(__printf__, 1, 2)));
#define printf emu_printf
