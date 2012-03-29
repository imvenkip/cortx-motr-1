/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __COLIBRI_STOB_LINUX_H__
#define __COLIBRI_STOB_LINUX_H__

/**
   @defgroup stoblinux Storage object based on Linux specific file system
   and block device interfaces.

   @see stob
   @{
 */

extern struct c2_stob_type c2_linux_stob_type;

int  c2_linux_stobs_init(void);
void c2_linux_stobs_fini(void);

struct c2_stob_domain;

int c2_linux_stob_setup(struct c2_stob_domain *dom, bool use_directio);
int c2_linux_stob_link(struct c2_stob_domain *dom, struct c2_stob *obj,
			const char *path, struct c2_dtx *tx);

/** @} end group stoblinux */

/* __COLIBRI_STOB_LINUX_H__ */
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
