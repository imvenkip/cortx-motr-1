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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 02/23/2011
 */

#ifndef __COLIBRI_SITE_SITE_H__
#define __COLIBRI_SITE_SITE_H__

/**
   @defgroup site Site

   @{
 */

/* import */
struct c2_md_store;
struct c2_io_store;
struct c2_list_link;

struct c2_site {
        struct c2_md_store     *s_mdstore;
        struct c2_io_store     *s_iostore;
        struct c2_list_link     s_linkage;
};

/**
   Init site using metadata and data stores.
 */
int c2_site_init(struct c2_site *s, 
                 struct c2_md_store *md);

void c2_site_fini(struct c2_site *s);

int c2_sites_init(void);
void c2_sites_fini(void);

/** @} endgroup site */

/* __COLIBRI_SITE_SITE_H__ */
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
