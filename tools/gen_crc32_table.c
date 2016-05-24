/*
 * gen_crc32_table.c - a program for CRC-32 table generation
 *
 * Written in 2014-2015 by Eric Biggers <ebiggers3@gmail.com>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t crc32_table[0x800];
static uint32_t crc32_rolling[0x100];

static uint32_t
crc32_update_bit(uint32_t remainder, uint8_t next_bit)
{
	return (remainder >> 1) ^
		(((remainder ^ next_bit) & 1) ? 0xEDB88320 : 0);
}

static uint32_t
crc32_update_byte(uint32_t remainder, uint8_t next_byte)
{
	for (int j = 0; j < 8; j++, next_byte >>= 1)
		remainder = crc32_update_bit(remainder, next_byte & 1);
	return remainder;
}

static void
print_256_entries(const uint32_t *entries)
{
	for (size_t i = 0; i < 256 / 4; i++) {
		printf("\t");
		for (size_t j = 0; j < 4; j++) {
			printf("0x%08x,", entries[i * 4 + j]);
			if (j != 3)
				printf(" ");
		}
		printf("\n");
	}
}

int
main(void)
{
	/* crc32_table[i] for 0 <= i < 0x100 is the CRC-32 of byte i.  */
	for (int i = 0; i < 0x100; i++)
		crc32_table[i] = crc32_update_byte(0, i);

	/* crc32_table[i] for 0x100 <= i < 0x800 is the CRC-32 of byte i % 0x100
	 * followed by i / 0x100 zero bytes.  */
	for (int i = 0x100; i < 0x800; i++)
		crc32_table[i] = crc32_update_byte(crc32_table[i - 0x100], 0);

	for (int i = 0; i < 0x100; i++) {
		uint32_t remainder = crc32_update_byte(0, i);
		for (int j = 0; j < 16; j++)
			remainder = crc32_update_byte(remainder, 0);
		crc32_rolling[i] = remainder;
	}

	printf("/*\n");
	printf(" * crc32_table.h - data table to accelerate CRC-32 computation\n");
	printf(" *\n");
	printf(" * THIS FILE WAS AUTOMATICALLY GENERATED "
	       "BY gen_crc32_table.c.  DO NOT EDIT.\n");
	printf(" */\n");
	printf("\n");
	printf("#include <stdint.h>\n");
	printf("\n");
	printf("static const uint32_t crc32_table[] = {\n");
	print_256_entries(&crc32_table[0x000]);
	printf("#if defined(CRC32_SLICE4) || defined(CRC32_SLICE8)\n");
	print_256_entries(&crc32_table[0x100]);
	print_256_entries(&crc32_table[0x200]);
	print_256_entries(&crc32_table[0x300]);
	printf("#endif /* CRC32_SLICE4 || CRC32_SLICE8 */\n");
	printf("#if defined(CRC32_SLICE8)\n");
	print_256_entries(&crc32_table[0x400]);
	print_256_entries(&crc32_table[0x500]);
	print_256_entries(&crc32_table[0x600]);
	print_256_entries(&crc32_table[0x700]);
	printf("#endif /* CRC32_SLICE8 */\n");
	printf("};\n");

	printf("\n");
	printf("static const uint32_t crc32_rolling[] = {\n");
	print_256_entries(crc32_rolling);
	printf("};\n");

	/*
	 * test
	 */

	/*const char *str = "abcdefghijklmnop aoeuaoeu abcdefghijklmnop aoeuaoeuhaoneuhaoeu aoeuaoeuhaoneuhaoeu";*/
	/*size_t len = strlen(str);*/
	/*uint32_t remainder = 0;*/
	/*uint32_t hashes[len];*/
	/*size_t i = 0;*/
	/*for (; i < 16; i++)*/
		/*remainder = crc32_update_byte(remainder, str[i]);*/
	/*for (; i < len; i++) {*/
		/*remainder = crc32_update_byte(remainder, str[i]) ^ crc32_rolling[str[i - 16]];*/
		/*hashes[i] = remainder;*/
		/*for (size_t j = 0; j < i; j++)*/
			/*if (remainder == hashes[j])*/
				/*printf("MATCH: %zu:%.*s %zu:%.*s\n", j-15, 16, &str[j-15], i-15, 16, &str[i-15]);*/
	/*}*/

	return 0;
}
