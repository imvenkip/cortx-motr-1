#ifndef __COLIBRI___TEST_GCCXML_SIMPLE_H__
#define __COLIBRI___TEST_GCCXML_SIMPLE_H__

#include <sys/types.h>
#include <stdint.h>

#include "xcode/xcode.h"


struct fid {
	uint64_t f_container;
	uint64_t f_offset;
} C2_XCA_RECORD;

struct optfid {
	uint8_t o_flag;
	union {
		struct fid o_fid C2_XCA_TAG("1");
		uint32_t o_short C2_XCA_TAG("3");
	} u;
} C2_XCA_UNION;

struct optfidarray {
	uint64_t ofa_nr;
	struct optfid *ofa_data;
} C2_XCA_SEQUENCE;

enum {
	NR = 9
};

struct fixarray {
	c2_void_t fa_none C2_XCA_TAG("NR");
	struct optfid *fa_data;
} C2_XCA_SEQUENCE;

struct testtypes {
    char          tt_char;
    char         *tt_pchar;
    const char   *tt_cpchar;
    int           tt_int;
    long long     tt_ll;
    unsigned int  tt_ui;
} C2_XCA_RECORD;


/* __COLIBRI___TEST_GCCXML_SIMPLE_H__ */
#endif

