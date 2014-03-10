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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09/09/2010
 */

#pragma once

#ifndef __MERO_FID_FID_H__
#define __MERO_FID_FID_H__

/**
   @defgroup fid File identifier

   @{
 */

/* import */
#include "lib/types.h"
#include "xcode/xcode_attr.h"

/* export */
struct m0_fid_type;
struct m0_fid;

struct m0_fid {
        uint64_t f_container;
        uint64_t f_key;
} M0_XCA_RECORD;

M0_INTERNAL bool m0_fid_is_set(const struct m0_fid *fid);
M0_INTERNAL bool m0_fid_is_valid(const struct m0_fid *fid);
M0_INTERNAL bool m0_fid_eq(const struct m0_fid *fid0,
			   const struct m0_fid *fid1);
M0_INTERNAL int m0_fid_cmp(const struct m0_fid *fid0,
			   const struct m0_fid *fid1);
M0_INTERNAL void m0_fid_set(struct m0_fid *fid,
                            uint64_t container,
			    uint64_t key);

M0_INTERNAL int m0_fid_sscanf(const char *s, struct m0_fid *fid);

M0_INTERNAL int m0_fid_init(void);
M0_INTERNAL void m0_fid_fini(void);

#define FID_F "<%lx:%lx>"
#define FID_P(f) (unsigned long)(f)->f_container, (unsigned long)(f)->f_key

#define M0_FID_INIT(container, key)		\
	((struct m0_fid) {			\
		.f_container = (container),	\
		.f_key = (key)			\
	})

#define M0_FID0 { 0ULL, 0ULL }

#define M0_FID_BUF(fid) ((struct m0_buf){	\
	.b_nob = sizeof *(fid),			\
	.b_addr = (fid)				\
})

struct m0_fid_type {
	uint8_t     ft_id;
	const char *ft_name;
	bool      (*ft_is_valid)(const struct m0_fid *fid);
};

M0_INTERNAL void m0_fid_type_register(const struct m0_fid_type *fidt);
M0_INTERNAL const struct m0_fid_type *m0_fid_type_get(uint8_t id);
M0_INTERNAL const struct m0_fid_type *m0_fid_type_gethi(uint64_t id);
M0_INTERNAL const struct m0_fid_type *
m0_fid_type_getfid(const struct m0_fid *fid);
M0_INTERNAL const struct m0_fid_type *m0_fid_type_getname(const char *name);

/** @} end of fid group */
#endif /* __MERO_FID_FID_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
