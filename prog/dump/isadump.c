/*
    isadump.c - isadump, a user-space program to dump ISA registers
    Copyright (C) 2000  Frodo Looijaard <frodol@dds.nl>, and
                        Mark D. Studebaker <mdsxyz123@yahoo.com>
    Copyright (C) 2004  The lm_sensors group

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
	Typical usage:
	isadump 0x295 0x296		Basic winbond dump using address/data registers
	isadump 0x295 0x296 2		Winbond dump, bank 2
	isadump 0x2e 0x2f 0x09		Super-I/O, logical device 9
	isadump -f 0x5000		Flat address space dump like for Via 686a
	isadump -f 0xecf0 0x10 1	PC87366, temperature channel 2
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


/* To keep glibc2 happy */
#if defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ >= 0
#include <sys/io.h>
#else
#include <asm/io.h>
#endif

#ifdef __powerpc__
unsigned long isa_io_base = 0; /* XXX for now */
#endif /* __powerpc__ */

char hexchar(int i)
{
	if ((i >= 0) && (i <= 9))
		return '0' + i;
	else if (i <= 15)
		return 'a' - 10 + i;
	else
		return 'X';
}

void help(void)
{
	fprintf(stderr,
	        "Syntax for I2C-like access:\n"
	        "  isadump ADDRREG DATAREG [BANK [BANKREG]]\n"
	        "Syntax for flat address space:\n"
	        "  isadump -f ADDRESS [RANGE [BANK [BANKREG]]]\n");
}

int default_bankreg(int flat, int addrreg, int datareg)
{
	if (flat) {
		return 0x09; /* Works for National Semiconductor
		                Super-IO chips */
	}

	if ((addrreg == 0x2e && datareg == 0x2f)
	 || (addrreg == 0x4e && datareg == 0x4f)) {
		return 0x07; /* Works for all Super-I/O chips */
	}
	
	return 0x4e; /* Works for Winbond ISA chips, default */
}

int set_bank(int flat, int addrreg, int datareg, int bank, int bankreg)
{
	int oldbank;

	if (flat) {
		oldbank = inb(addrreg+bankreg);
		outb(bank, addrreg+bankreg);
	} else {
		outb(bankreg, addrreg);
		oldbank = inb(datareg);
		outb(bank, datareg);
	}

	return oldbank;
}

int main(int argc, char *argv[])
{
	int addrreg;        /* address in flat mode */
	int datareg = 0;    /* unused in flat mode */
	int range = 256;    /* can be changed only in flat mode */
	int bank = -1;      /* -1 means no bank operation */
	int bankreg;
	int oldbank = 0;
	int i, j, res;
	int flat = 0;
	char *end;

	if (argc < 3) {
		help();
		exit(1);
	}

	if (!strcmp(argv[1], "-f")) {
		flat = 1;
	}

	addrreg = strtol(argv[1+flat], &end, 0);
	if (*end) {
		fprintf(stderr, "Error: Invalid address!\n");
		help();
		exit(1);
	}
	if (addrreg < 0 || addrreg > (flat?0xffff:0x3fff)) {
		fprintf(stderr, "Error: Address out of range "
		        "(0x0000-0x%04x)!\n", flat?0xffff:0x3fff);
		help();
		exit(1);
	}

	if (flat) {
		if (argc > 3) {
			range = strtol(argv[3], &end, 0);
			if (*end || range <= 0 || range > 0x100
			 || range & 0xf) {
				fprintf(stderr, "Error: Invalid range!\n"
				        "Hint: Must be a multiple of 16 no "
				        "greater than 256.\n");
				help();
				exit(1);
			}
		} else {
			addrreg &= 0xff00; /* Force alignment */
		}
	} else {
		datareg = strtol(argv[2], &end, 0);
		if (*end) {
			fprintf(stderr, "Error: Invalid data register!\n");
			help();
			exit(1);
		}
		if (datareg < 0 || datareg > 0x3fff) {
			fprintf(stderr, "Error: Data register out of range "
			        "(0x0000-0x3fff)!\n");
			help();
			exit(1);
		}
	}

	bankreg = default_bankreg(flat, addrreg, datareg);

	if (argc > 3+flat) {
		bank = strtol(argv[3+flat], &end, 0);
		if (*end) {
			fprintf(stderr, "Error: Invalid bank number!\n");
			help();
			exit(1);
		}
		if ((bank < 0) || (bank > 15)) {
			fprintf(stderr, "Error: bank out of range (0-15)!\n");
			help();
			exit(1);
		}

		if (argc > 4+flat) {
			bankreg = strtol(argv[4+flat], &end, 0);
			if (*end) {
				fprintf(stderr, "Error: Invalid bank "
				        "register!\n");
				help();
				exit(1);
			}
			if (bankreg < 0 || bankreg >= range) {
				fprintf(stderr, "Error: bank out of range "
				        "(0x00-0x%02x)!\n", range-1);
				help();
				exit(1);
			}
		}
	}

	if (getuid()) {
		fprintf(stderr, "Error: Can only be run as root (or make it "
		        "suid root)\n");
		exit(1);
	}

	fprintf(stderr, "WARNING! Running this program can cause system "
	        "crashes, data loss and worse!\n");
	if (flat)
		fprintf(stderr, "I will probe address range 0x%04x to "
		        "0x%04x.\n", addrreg, addrreg + range - 1);
	else
		fprintf(stderr, "I will probe address register 0x%04x and "
		        "data register 0x%04x.\n", addrreg, datareg);
	if (bank>=0) 	
		fprintf(stderr, "Probing bank %d using bank register "
		        "0x%02x.\n", bank, bankreg);
	fprintf(stderr, "You have five seconds to reconsider and press "
	        "CTRL-C!\n\n");
	sleep(5);

#ifndef __powerpc__
	if ((datareg < 0x400) && (addrreg < 0x400) && !flat) {
		if (ioperm(datareg, 1, 1)) {
			fprintf(stderr, "Error: Could not ioperm() data "
			        "register!\n");
			exit(1);
		}
		if (ioperm(addrreg, 1, 1)) {
			fprintf(stderr, "Error: Could not ioperm() address "
			        "register!\n");
			exit(1);
		}
	} else {
		if (iopl(3)) {
			fprintf(stderr, "Error: Could not do iopl(3)!\n");
			exit(1);
		}
	}
#endif

	if (bank >= 0)
		oldbank = set_bank(flat, addrreg, datareg, bank, bankreg);

	printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
	for (i = 0; i < range; i += 16) {
		printf("%c0: ", hexchar(i/16));
		for (j = 0; j < 16; j++) {
			if (flat) {
				res = inb(addrreg + i + j);
			} else {	
				outb(i+j, addrreg);
				res = inb(datareg);
			}
			printf("%c%c ", hexchar(res/16), hexchar(res%16));
		}
		printf("\n");
	}

	/* Restore the original bank value */
	if (bank >= 0)
		set_bank(flat, addrreg, datareg, oldbank, bankreg);

	exit(0);
}
