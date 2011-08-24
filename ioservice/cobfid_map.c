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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 08/23/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "ioservice/cobfid_map.h"

/**
 * @addtogroup cobfidmap
 * @{
 */

enum {
	C2_CFM_MAP_MAGIC  = 0x6d4d4643000a7061,
	C2_CFM_ITER_MAGIC = 0x694d46430a726574,
	C2_CFM_ITER_THUNK = 16 /* #records in an iter buffer */
};

/**
   Internal data structure used to store a record in the iterator buffer.
 */
struct c2_cobfid_record {
	uint64_t          cfr_ci;  /**< container id */
	struct c2_fid     cfr_fid; /**< global file id */
	struct c2_uint128 cfr_cob; /**< cob id */
};


/*
 *****************************************************************************
 struct c2_cobfid_map
 *****************************************************************************
 */

/**
   Invariant for the c2_cobfid_map.
 */
static bool cobfid_map_invariant(const struct c2_cobfid_map *cfm)
{
	if (cfm->cfm_magic != C2_CFM_MAP_MAGIC)
		return false;
	return true;
}

void c2_cobfid_map_fini(struct c2_cobfid_map *cfm)
{
	C2_PRE(cobfid_map_invariant(cfm));
}
C2_EXPORTED(c2_cobfid_map_fini);


/*
 *****************************************************************************
 struct c2_cobfid_map_iter
 *****************************************************************************
 */

/**
   Invariant for the c2_cobfid_map_iter.
 */
static bool cobfid_map_iter_invariant(const struct c2_cobfid_map_iter *iter)
{
	if (iter->cfmi_magic != C2_CFM_ITER_MAGIC)
		return false;
	if (!cobfid_map_invariant(iter->cfmi_cfm))
		return false;
	if (iter->cfmi_magic == 0 || iter->cfmi_magic >= CFM_QT_ENUM_NR)
		return false;
	if (iter->cfmi_query == NULL)
		return false;
	return true;
}

void c2_cobfid_map_iter_fini(struct  c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));
}
C2_EXPORTED(c2_cobfid_map_iter_fini);

/**
   Internal sub to initialize an iterator.
 */
static int
cobfid_map_iter_init(struct c2_cobfid_map *cfm,
		     struct c2_cobfid_map_iter *iter,
		     int (*qsub)(struct c2_cobfid_map_iter *))
{
	iter->cfmi_cfm = cfm;
	iter->cfmi_query = qsub;

	/* allocate buffer */

	iter->cfmi_rec_idx = iter->cfmi_num_recs; /* force a query */

	iter->cfmi_magic = C2_CFM_ITER_MAGIC;
	C2_POST(cobfid_map_iter_invariant(iter));
	return 0;
}


/*
 *****************************************************************************
 Traversal set up the iterator with specific query subroutines which will
 be invoked from the iterator's next() routine to fill the buffer when empty.
 *****************************************************************************
 */

static int qsub_container_enum(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	return 0;
}

int c2_cobfid_map_container_enum(struct c2_cobfid_map *cfm,
				 uint64_t container_id,
				 struct c2_cobfid_map_iter *iter)
{
	int rc;

	rc = cobfid_map_iter_init(cfm, iter, qsub_container_enum);
	iter->cfmi_next_ci = container_id;
	return rc;
}
C2_EXPORTED(c2_cobfid_map_container_enum);


static int qsub_map_enum(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	return 0;
}

int c2_cobfid_map_enum(struct c2_cobfid_map *cfm,
		       struct c2_cobfid_map_iter *iter)
{
	int rc;

	rc = cobfid_map_iter_init(cfm, iter, qsub_map_enum);
	return rc;

}
C2_EXPORTED(c2_cobfid_map_enum);

/** @} cobfidmap */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
