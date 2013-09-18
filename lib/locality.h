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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04-Jun-2013
 */

#pragma once

#ifndef __MERO_LIB_LOCALITY_H__
#define __MERO_LIB_LOCALITY_H__

/**
 * @defgroup locality
 *
 * @{
 */

/* import */
#include "lib/types.h"
#include "lib/processor.h"
struct m0_sm_group;
struct m0_reqh;

/* export */
struct m0_locality;

/**
 * Per-core state maintained by Mero.
 */
struct m0_locality {
	/**
	 * State machine group associated with the core.
	 *
	 * This group can be used to post ASTs to be executed on a current or
	 * specified core.
	 *
	 * This group comes from request handler locality, so that execution of
	 * ASTs is serialised with state transitions of foms in this locality.
	 */
	struct m0_sm_group *lo_grp;
	struct m0_reqh     *lo_reqh;
	size_t              lo_idx;
	/* Other fields might be added here. */
};

/**
 * Returns locality corresponding to the core the call is made on.
 *
 * @post result->lo_grp != NULL
 */
M0_INTERNAL struct m0_locality *m0_locality_here(void);

/**
 * Returns locality corresponding in some unspecified, but deterministic way to
 * the supplied value.
 *
 * @post result->lo_grp != NULL
 */
M0_INTERNAL struct m0_locality *m0_locality_get(uint64_t value);

M0_INTERNAL struct m0_locality *m0_locality0_get(void);

/**
 * Initialises per-core state for the specified core.
 *
 * This has effect only once per-core.
 */
M0_INTERNAL void m0_locality_set(m0_processor_nr_t id, struct m0_locality *val);

M0_INTERNAL int  m0_localities_init(void);
M0_INTERNAL void m0_localities_fini(void);

/** @} end of locality group */

#endif /* __MERO_LIB_LOCALITY_H__ */

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
