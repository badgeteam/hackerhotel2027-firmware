#include <string.h>
#include "ch32fun.h"
#include "flash_pages.h"

// --- Low-level FPEC sequences (CH643RM.PDF 20.4) ---------------------------
// Register names/bit positions confirmed against ch32x03xhw.h's
// FLASH_TypeDef (ACTLR/KEYR/OBKEYR/STATR/CTLR/ADDR/.../MODEKEYR, matching
// the manual's register offsets exactly) -- this chip shares the same FPEC
// block as the RISC-V v20x/v30x parts ch32fun already has flash helpers
// for (extralibs/ch20x_30x_flash.h), just with CH643-specific sizing (62KB
// total, 256-byte fast-program/erase pages). Verified on real hardware
// that 0x08000000-based addresses and the 0x00000000 the linker places
// code at are the same physical flash (byte-identical SWD reads at both)
// before writing this.

static void flash_wait_busy( void )
{
	while( FLASH->STATR & FLASH_STATR_BSY ) {}
}

static void flash_clear_status( void )
{
	FLASH->STATR = FLASH_STATR_EOP | FLASH_STATR_WRPRTERR;
}

static void flash_unlock( void )
{
	FLASH->KEYR = FLASH_KEY1;
	FLASH->KEYR = FLASH_KEY2;
	FLASH->MODEKEYR = FLASH_KEY1;
	FLASH->MODEKEYR = FLASH_KEY2;
}

static void flash_lock( void )
{
	FLASH->CTLR |= FLASH_CTLR_LOCK | FLASH_CTLR_FLOCK;
}

// Erases one 256-byte page at an absolute (0x08000000-based) address.
// CH643RM.PDF 20.4.6, "Main Memory Fast Erase" (256-byte variant).
static void flash_erase_page( uint32_t addr )
{
	flash_wait_busy();
	FLASH->CTLR |= FLASH_CTLR_FTER;
	FLASH->ADDR = addr;
	FLASH->CTLR |= FLASH_CTLR_STRT;
	flash_wait_busy();
	flash_clear_status();
	FLASH->CTLR &= ~FLASH_CTLR_FTER;
}

// Programs one full 256-byte page at an absolute (0x08000000-based),
// page-aligned address. CH643RM.PDF 20.4.5, "Main Memory Fast
// Programming" -- the whole 256 bytes commits atomically on STRT, so
// `data` must already be the complete page content (callers pad the
// final partial page with 0xFF before calling this).
static void flash_program_page( uint32_t addr, const uint8_t *data )
{
	flash_wait_busy();
	FLASH->CTLR |= FLASH_CTLR_FTPG;
	FLASH->CTLR |= FLASH_CTLR_BUFRST;
	flash_wait_busy();
	for( unsigned i = 0; i < FLASH_PAGE_SIZE / 4; i++ )
	{
		uint32_t word;
		memcpy( &word, data + i * 4, 4 );
		*(volatile uint32_t *)( addr + i * 4 ) = word;
		FLASH->CTLR |= FLASH_CTLR_BUFLOAD;
		flash_wait_busy();
	}
	FLASH->ADDR = addr;
	FLASH->CTLR |= FLASH_CTLR_STRT;
	flash_wait_busy();
	flash_clear_status();
	FLASH->CTLR &= ~FLASH_CTLR_FTPG;
}

// --- Public API --------------------------------------------------------

void FlashPages_Erase(uint32_t addr) {
    const uint32_t* p = (const uint32_t*)addr;
    unsigned i = 0;
    while (i < FLASH_PAGE_SIZE / 4 && p[i] == 0xFFFFFFFFu) i++;
    if (i == FLASH_PAGE_SIZE / 4) return;  // erased already
    flash_unlock();
    flash_erase_page(addr);
    flash_lock();
}

void FlashPages_Program(uint32_t addr, const uint8_t* data) {
    flash_unlock();
    flash_program_page(addr, data);
    flash_lock();
}
