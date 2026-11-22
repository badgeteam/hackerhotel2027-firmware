#include "bootloader.h"
#include "ch32fun.h"

void jump_to_bootloader(void) {
    __disable_irq();

    // Unlock BOOT_MODE (CH643RM 20.3.10), then select the BOOT area (system
    // bootloader ROM) as the code source after the next software reset
    // (FLASH_STATR.BOOT_MODE, CH643RM 20.3.4)
    FLASH->BOOT_MODEKEYR = FLASH_KEY1;
    FLASH->BOOT_MODEKEYR = FLASH_KEY2;
    FLASH->STATR = FLASH_STATR_BOOT_MODE;

    RCC->RSTSCKR |= RCC_RMVF;  // clear reset flags so the bootloader sees a clean software reset
    PFIC->SCTLR = 1u << 31;    // SYSRST: software system reset
    while (1);
}
