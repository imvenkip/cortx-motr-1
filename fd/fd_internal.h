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
 * Original author: Nachiket Sahasrabudhe <nachiket_sahasrabuddhe@xyratex.com>
 * Original creation date: 12-Jan-15
 */

#pragma once
#ifndef __MERO_LAYOUT_FD_INT_H__
#define __MERO_LAYOUT_FD_INT_H__

#include "fd/fd.h"
#include "conf/obj.h" /* Pool version. */
#include "lib/misc.h" /* uint16_t, uint32_t etc. */

/** Calculates the tile parameters using pdclust layout. */
M0_INTERNAL int m0_fd__tile_init(struct m0_fd_tile *tile,
				 const struct m0_pdclust_attr *la_attr,
				 uint64_t *children_nr, uint64_t depth);
/** Populates the tile with appropriate values. */
M0_INTERNAL void m0_fd__tile_populate(struct m0_fd_tile *tile);

#endif
