/*
 * boot_test.c
 *
 * Created: 7/5/2022 9:10:31 AM
 * Author : feros
 */ 

#include <avr/io.h>
#include <avr/boot.h>

#include <inttypes.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

void RWWSection() {
	PORTB = ~PORTB; 
}


// uses 136 out of 256 bytes
BOOTLOADER_SECTION
void  boot_program_page (uint32_t page, const uint8_t *buf)
{
	uint16_t i;

	eeprom_busy_wait ();

	
	for (i=0; i<SPM_PAGESIZE; i+=2)
	{
		// Set up little-endian word.

		uint16_t w = *buf++;
		w += (*buf++) << 8;
		
		boot_page_fill (page + i, w);
	}

	boot_page_erase (page);
	boot_spm_busy_wait ();      // Wait until the memory is erased.


	boot_page_write (page);     // Store buffer in flash page.
	boot_spm_busy_wait();       // Wait until the memory is written.

	// Reenable RWW-section again. We need this if we want to jump back
	// to the application after bootloading.

	boot_rww_enable ();
}

const uint8_t my_data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127};

int main(void)
{
    /* Replace with your application code */
    while (1) 
    {
		boot_program_page(0x1000-256, my_data);
    }
}

