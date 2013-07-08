/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 08/30/2012
 */

#pragma once

#ifndef __MERO_LIB_TRACE_INTERNAL_H__
#define __MERO_LIB_TRACE_INTERNAL_H__

M0_INTERNAL int m0_arch_trace_init(uint32_t logbuf_size);

M0_INTERNAL void m0_arch_trace_fini(void);

M0_INTERNAL int
m0_trace_subsys_list_to_mask(char *subsys_names, unsigned long *ret_mask);

M0_INTERNAL enum m0_trace_level
m0_trace_parse_trace_level(char *str);

M0_INTERNAL enum m0_trace_print_context
m0_trace_parse_trace_print_context(const char *ctx_name);

M0_INTERNAL const char *m0_trace_level_name(enum m0_trace_level level);

M0_INTERNAL const char *m0_trace_subsys_name(uint64_t subsys);

union m0_trace_rec_argument {
	uint8_t  v8;
	uint16_t v16;
	uint32_t v32;
	uint64_t v64;
};

typedef union m0_trace_rec_argument m0_trace_rec_args_t[M0_TRACE_ARGC_MAX];

M0_INTERNAL void m0_trace_unpack_args(const struct m0_trace_rec_header *trh,
				      m0_trace_rec_args_t args,
				      const void *buf);

M0_INTERNAL void *m0_trace_get_logbuf_addr(void);
M0_INTERNAL uint32_t m0_trace_get_logbuf_size(void);
M0_INTERNAL uint64_t m0_trace_get_logbuf_pos(void);

M0_INTERNAL const struct m0_trace_rec_header *m0_trace_get_last_record(void);

M0_INTERNAL void m0_trace_update_stats(uint32_t rec_size);

#endif /* __MERO_LIB_TRACE_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
