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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/28/2010
 */

#pragma once

#ifndef __MERO_STOB_LINUX_H__
#define __MERO_STOB_LINUX_H__

/**
   @defgroup stoblinux Storage object based on Linux specific file system
   and block device interfaces.

   @see stob
   @{
 */

extern struct m0_stob_type m0_linux_stob_type;

M0_INTERNAL int m0_linux_stobs_init(void);
M0_INTERNAL void m0_linux_stobs_fini(void);

struct m0_stob_domain;
struct m0_dtx;

M0_INTERNAL int m0_linux_stob_setup(struct m0_stob_domain *dom,
				    bool use_directio);
M0_INTERNAL int m0_linux_stob_link(struct m0_stob_domain *dom,
				   struct m0_stob *obj, const char *path,
				   struct m0_dtx *tx);

M0_INTERNAL int m0_linux_stob_domain_locate(const char *domain_name,
				            struct m0_stob_domain **dom,
				            uint64_t dom_id);

M0_INTERNAL int m0_linux_stob_ino(struct m0_stob *stob);

/** @} end group stoblinux */

/* __MERO_STOB_LINUX_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
