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

#ifndef __COLIBRI_STOB_STOB_ID_H__
#define __COLIBRI_STOB_STOB_ID_H__

#include "lib/arith.h"

/**
   Unique storage object identifier.

   A storage object in a cluster is identified by identifier of this type.
 */
struct c2_stob_id {
	struct c2_uint128 si_bits;
};

bool c2_stob_id_eq (const struct c2_stob_id *id0, const struct c2_stob_id *id1);
int  c2_stob_id_cmp(const struct c2_stob_id *id0, const struct c2_stob_id *id1);
bool c2_stob_id_is_set(const struct c2_stob_id *id);

/* __COLIBRI_STOB_STOB_ID_H__ */
#endif
