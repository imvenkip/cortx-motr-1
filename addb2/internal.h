/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 4-Feb-2015
 */

#pragma once

#ifndef __MERO_ADDB2_INTERNAL_H__
#define __MERO_ADDB2_INTERNAL_H__

/**
 * @defgroup addb2
 *
 * @{
 */

#define HEADER_XO(h) &(struct m0_xcode_obj) {	\
	.xo_type = m0_addb2_frame_header_xc,	\
	.xo_ptr  = (h)				\
}

#define TRACE_XO(t) &(struct m0_xcode_obj) {	\
	.xo_type = m0_addb2_trace_xc,		\
	.xo_ptr  = (t)				\
}

enum {
	FRAME_TRACE_MAX = 128,
	FRAME_SIZE_MAX  = 4 * 1024 * 1024,
};

M0_INTERNAL m0_bcount_t m0_addb2_trace_size(const struct m0_addb2_trace *trace);

M0_EXTERN uint64_t m0_addb2__dummy_payload[];
M0_EXTERN uint64_t m0_addb2__dummy_payload_size;

M0_TL_DESCR_DECLARE(tr, M0_EXTERN);
M0_TL_DECLARE(tr, M0_INTERNAL, struct m0_addb2_trace_obj);

M0_TL_DESCR_DECLARE(mach, M0_EXTERN);
M0_TL_DECLARE(mach, M0_INTERNAL, struct m0_addb2_mach);

M0_INTERNAL void m0_addb2__mach_print(const struct m0_addb2_mach *m);

M0_EXTERN const struct m0_fom_type_ops m0_addb2__fom_type_ops;
M0_EXTERN const struct m0_sm_conf      m0_addb2__sm_conf;

/** @} end of addb2 group */
#endif /* __MERO_ADDB2_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
