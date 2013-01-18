/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 9-Jan-2013
 */

#include "lib/lockers.h" /* m0_lockers */
#include "lib/types.h"   /* uint32_t */
#include "lib/string.h"  /* memset */
#include "lib/assert.h"  /* M0_PRE */
#include "lib/misc.h"    /* M0_SET0 */

/**
 * @addtogroup lockers
 *
 * @{
 */

M0_INTERNAL void m0_lockers_init(const struct m0_lockers_type *lt,
				 struct m0_lockers            *lockers)
{
	memset(lockers->loc_slots, 0,
	       lt->lot_max * sizeof lockers->loc_slots[0]);
}

M0_INTERNAL int m0_lockers_allot(struct m0_lockers_type *lt)
{
	M0_PRE(lt->lot_count < lt->lot_max);
	return lt->lot_count++;
}

M0_INTERNAL void m0_lockers_set(const struct m0_lockers_type *lt,
				struct m0_lockers            *lockers,
				uint32_t                      key,
				void                         *data)
{
	M0_PRE(key < lt->lot_max);
	M0_PRE(m0_lockers_is_empty(lt, lockers, key));
	lockers->loc_slots[key] = data;
}

M0_INTERNAL void *m0_lockers_get(const struct m0_lockers_type *lt,
				 const struct m0_lockers      *lockers,
				 uint32_t                      key)
{
	M0_PRE(key < lt->lot_max);
	return lockers->loc_slots[key];
}

M0_INTERNAL void m0_lockers_clear(const struct m0_lockers_type *lt,
				  struct m0_lockers            *lockers,
				  uint32_t                      key)
{
	M0_PRE(key < lt->lot_max);
	M0_PRE(!m0_lockers_is_empty(lt, lockers, key));
	lockers->loc_slots[key] = NULL;
}

M0_INTERNAL bool m0_lockers_is_empty(const struct m0_lockers_type *lt,
				     const struct m0_lockers      *lockers,
				     uint32_t                      key)
{
	M0_PRE(key < lt->lot_max);
	return lockers->loc_slots[key] == NULL;
}

M0_INTERNAL void m0_lockers_fini(struct m0_lockers_type *lt,
				 struct m0_lockers      *lockers)
{

}

/** @} end of lockers group */


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
