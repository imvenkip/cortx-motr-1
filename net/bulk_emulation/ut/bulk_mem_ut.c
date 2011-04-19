/* -*- C -*- */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "lib/ut.h"

#include "net/bulk_emulation/mem_xprt_xo.c"

void test_buf_copy(void)
{
	/* Create buffers with different shapes but same total size */
	enum { NR_BUFS = 5 };
	static struct {
		uint32_t    num_segs;
		c2_bcount_t seg_size;
	} shapes[NR_BUFS] = {
		[0] = { 1, 48 },
		[1] = { 2, 24 },
		[2] = { 3, 16 },
		[3] = { 4, 12 },
		[4] = { 6,  8 },
	};
	static const char *msg = "abcdefghijklmnopqrstuvwxyz0123456789"
		"ABCDEFGHIJK";
	size_t msglen = strlen(msg)+1;
	struct c2_net_buffer bufs[NR_BUFS];
	int i;
	struct c2_net_buffer *nb;

	C2_SET_ARR0(bufs);
	for (i=0; i < NR_BUFS; i++) {
		C2_UT_ASSERT(msglen == shapes[i].num_segs * shapes[i].seg_size);
		C2_UT_ASSERT(c2_bufvec_alloc(&bufs[i].nb_buffer, 
					     shapes[i].num_segs,
					     shapes[i].seg_size) == 0);
	}
	nb = &bufs[0]; /* single buffer */
	C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == 1);
	memcpy(nb->nb_buffer.ov_buf[0], msg, msglen);
	C2_UT_ASSERT(memcmp(nb->nb_buffer.ov_buf[0], msg, msglen) == 0);
	for (i=1; i < NR_BUFS; i++) {
		C2_UT_ASSERT(mem_copy_buffer(&bufs[i],&bufs[i-1],msglen) == 0);
		int j;
		const char *p = msg;
		for (j=0; j<bufs[i].nb_buffer.ov_vec.v_nr; j++) {
			int k;
			char *q;
			for (k=0; k<bufs[i].nb_buffer.ov_vec.v_count[j]; k++){
				q = bufs[i].nb_buffer.ov_buf[j] + k;
				C2_UT_ASSERT(*p++ == *q);
			}
		}

	}
	
}

const struct c2_test_suite net_bulk_mem_ut = {
        .ts_name = "net-bulk-mem",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_mem_buf_copy", test_buf_copy },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
