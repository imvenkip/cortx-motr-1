#ifndef __COLIBRI___TEST_GCCXML_H__
#define __COLIBRI___TEST_GCCXML_H__

#include <sys/types.h>
#include <stdint.h>

#include "xcode/xcode.h"
#include "lib/vec.h"
#include "lib/vec_xc.h"

#include "xcode/ut/test_gccxml_simple.h"
#include "xcode/ut/test_gccxml_simple_xc.h"


struct package {
	struct fid p_fid;
	struct c2_vec p_vec;
	struct c2_cred *p_cred C2_XCA_OPAQUE("c2_package_cred_get");
	struct package_p_name {
		uint32_t s_nr;
		uint8_t *s_data;
	} C2_XCA_SEQUENCE p_name;
} C2_XCA_RECORD;


/* __COLIBRI___TEST_GCCXML_H__ */
#endif

