/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>

#include <lib/c2bitops.h>

void test_bitops(void)
{
	long test;

	if (c2_fls(0) != -1) {
		printf("bad fls for 0\n");
	}

	if (c2_fls(1) != 0) {
		printf("bad fls for 1\n");
	}

	if (c2_fls(0x80000000) != 31) {
		printf("bad fls for 0x80000000\n");
	}
	
	test = 0x00000008;
	if (c2_find_next_bit(&test, 32, 8) != -1) {
		printf("bad find_next for 0x80000000\n");
	}

	test = 0x80000000;
	if (c2_find_next_bit(&test, 32, 0) != 31) {
		printf("bad find_next for 0x80000000\n");
	}

	test = 0x20000000;
	if (c2_find_next_bit(&test, 31, 0) != 29) {
		printf("bad find_next for 0x20000000 - 1\n");
	}

	test = 0x20000000;
	if (c2_find_next_bit(&test, 31, 8) != 29) {
		printf("bad find_next for 0x20000000 - 2\n");
	}

	if (c2_ffs(0) != -1) {
		printf("bad ffs for 0\n");
	}

	if (c2_ffs(1) != 0) {
		printf("bad ffs for 1\n");
	}

	if (c2_ffs(0x80000000) != 31) {
		printf("bad ffs for 0x80000000\n");
	}

	test = ~(0x00000008);
	if (c2_find_next_zero_bit(&test, 32, 8) != -1) {
		printf("bad find_next for ~0x00000008\n");
	}

	test = ~(0x80000000);
	if (c2_find_next_zero_bit(&test, 32, 0) != 31) {
		printf("bad find_next for ~0x80000000\n");
	}

	test = ~(0x20000000);
	if (c2_find_next_zero_bit(&test, 31, 0) != 29) {
		printf("bad find_next_zero for ~0x20000000\n");
	}

	test = ~(0x20000000);
	if (c2_find_next_zero_bit(&test, 31, 8) != 29) {
		printf("bad find_next_zero for ~0x20000000 - 2\n");
	}
}
