/* -*- C -*- */
/*
 * COPYRIGHT 2018 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author:  Rajanikant Chirmade <rajanikant.chirmade@seagate.com>
 * Original creation date: 25-Sept-2018
 */
#pragma once

#ifndef __MERO_CLOVIS_ST_UTILS_HELPER_H__
#define __MERO_CLOVIS_ST_UTILS_HELPER_H__

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "conf/obj.h"
#include "fid/fid.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"

int clovis_init(struct m0_clovis_config    *config,
	        struct m0_clovis_container *clovis_container,
	        struct m0_clovis          **clovis_instance);

void clovis_fini(struct m0_clovis *clovis_instance);

int clovis_touch(struct m0_clovis_container *clovis_container,
		 struct m0_uint128 id);

int clovis_write(struct m0_clovis_container *clovis_container,
		 char *src, struct m0_uint128 id,
		 uint32_t block_size, uint32_t block_count);

int clovis_read(struct m0_clovis_container *clovis_container,
		struct m0_uint128 id, char *dest,
		uint32_t block_size, uint32_t block_count);

int clovis_unlink(struct m0_clovis_container *clovis_container,
		  struct m0_uint128 id);

#endif /* __MERO_CLOVIS_ST_UTILS_HELPER_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
