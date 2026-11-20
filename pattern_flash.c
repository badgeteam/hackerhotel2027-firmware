#include <string.h>
#include "ch32fun.h"
#include "pattern_flash.h"

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
	for( unsigned i = 0; i < PATTERN_FLASH_PAGE / 4; i++ )
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

int PatternFlash_IsValid( void )
{
	const uint8_t *d = PatternFlash_Data();
	return d[0] == 'L' && d[1] == 'A' && d[2] == '0' && d[3] == '2';
}

void PatternFlash_EraseAll( void )
{
	flash_unlock();
	for( uint32_t off = 0; off < PATTERN_FLASH_SIZE; off += PATTERN_FLASH_PAGE )
		flash_erase_page( PATTERN_FLASH_BASE + off );
	flash_lock();
}

// Streaming-write state: how many bytes of the current page are filled,
// and the absolute flash address the next completed page goes to.
static uint8_t s_page_buf[PATTERN_FLASH_PAGE];
static unsigned s_page_fill;
static uint32_t s_write_addr;

void PatternFlash_BeginWrite( void )
{
	s_page_fill = 0;
	s_write_addr = PATTERN_FLASH_BASE;
}

static void flush_page( void )
{
	if( s_page_fill == 0 )
		return;
	if( s_page_fill < PATTERN_FLASH_PAGE )
		memset( s_page_buf + s_page_fill, 0xFF, PATTERN_FLASH_PAGE - s_page_fill );
	flash_unlock();
	flash_program_page( s_write_addr, s_page_buf );
	flash_lock();
	s_write_addr += PATTERN_FLASH_PAGE;
	s_page_fill = 0;
}

void PatternFlash_WriteBytes( const uint8_t *data, uint8_t len )
{
	// Bounds check: silently drop anything past the reserved region
	// rather than writing outside it.
	if( s_write_addr + s_page_fill + len > PATTERN_FLASH_BASE + PATTERN_FLASH_SIZE )
		return;
	for( uint8_t i = 0; i < len; i++ )
	{
		s_page_buf[s_page_fill++] = data[i];
		if( s_page_fill == PATTERN_FLASH_PAGE )
			flush_page();
	}
}

void PatternFlash_FinishWrite( void )
{
	flush_page();
}
