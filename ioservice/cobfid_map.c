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
#include "lib/misc.h"  /* SET0 */
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
	if (cfm == NULL || cfm->cfm_magic != C2_CFM_MAP_MAGIC)
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
	if (iter == NULL || iter->cfmi_magic != C2_CFM_ITER_MAGIC)
		return false;
	if (!cobfid_map_invariant(iter->cfmi_cfm))
		return false;
	if (iter->cfmi_qt == 0 || iter->cfmi_qt >= C2_COBFID_MAP_QT_NR)
		return false;
	if (iter->cfmi_ops == NULL ||
	    iter->cfmi_ops->cfmio_fetch == NULL ||
	    iter->cfmi_ops->cfmio_at_end == NULL ||
	    iter->cfmi_ops->cfmio_reload == NULL)
		return false;
	if (iter->cfmi_rec_idx > iter->cfmi_num_recs)
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
		     const struct c2_cobfid_map_iter_ops *ops)
{
	C2_PRE(cfm != NULL);
	C2_PRE(iter != NULL);
	C2_PRE(ops != NULL);

	C2_SET0(iter);
	iter->cfmi_cfm = cfm;
	iter->cfmi_ops = ops;

	/* allocate buffer */

	/* force a query by positioning at the end */
	iter->cfmi_rec_idx = iter->cfmi_num_recs;

	iter->cfmi_magic = C2_CFM_ITER_MAGIC;
	C2_POST(cobfid_map_iter_invariant(iter));
	return 0;
}

int c2_cobfid_map_iter_next(struct  c2_cobfid_map_iter *iter,
			    uint64_t *container_id_p,
			    struct c2_fid *file_fid_p,
			    struct c2_uint128 *cob_fid_p)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	if (iter->cfmi_error != 0)
		return iter->cfmi_error; /* already in error */

	if (iter->cfmi_last_load > iter->cfmi_cfm->cfm_last_mod) {
		/* iterator stale: use ce_reload and reset buffer index */
	} else if (iter->cfmi_rec_idx == iter->cfmi_num_recs) {
		/* buffer empty: use ce_fetch and then set cfmi_rec_idx to 0 */
	}

	/* Use ce_at_end to check if current record exhausts the iterator */

	/* save value of cfmi_last_ci and cfmi_last_fid before returning */
	return 0;
}
C2_EXPORTED(c2_cobfid_map_iter_next);


/*
 *****************************************************************************
 Enumerate container
 *****************************************************************************
 */

/**
   This subroutine fills the record buffer in the iterator.  It uses a
   database cursor to continue enumeration of the map table with starting
   key values based upon the cfmi_next_ci and cfmi_next_fid values.
   After loading the records, it sets cfmi_next_fid to the value of the
   fid in the last record read, so that the next fetch will continue
   beyond the current batch.  The value of cfmi_next_ci is not modified.
 */
static int enum_container_fetch(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	/* use c2_db_cursor_get() to read from (cfmi_next_ci, cfmi_next_fid) */

	return 0;
}

/**
   This subroutine returns true of the container_id of the current record
   is different from the value of cfmi_next_ci (which remains invariant
   for this query).
 */
static bool enum_container_at_end(struct c2_cobfid_map_iter *iter,
				  unsigned int idx)
{
	struct c2_cobfid_record *recs = iter->cfmi_buffer; /* safe cast */
	if (recs[idx].cfr_ci != iter->cfmi_next_ci)
		return false;
	return true;
}

/**
   Reload from the position prior to the last record read.
 */
static int enum_container_reload(struct c2_cobfid_map_iter *iter)
{
	iter->cfmi_next_fid = iter->cfmi_last_fid;
	return iter->cfmi_ops->cfmio_fetch(iter);
}

static const struct c2_cobfid_map_iter_ops enum_container_ops = {
	.cfmio_fetch  = enum_container_fetch,
	.cfmio_at_end = enum_container_at_end,
	.cfmio_reload = enum_container_reload
};

int c2_cobfid_map_container_enum(struct c2_cobfid_map *cfm,
				 uint64_t container_id,
				 struct c2_cobfid_map_iter *iter)
{
	int rc;

	rc = cobfid_map_iter_init(cfm, iter, &enum_container_ops);
	iter->cfmi_next_ci = container_id;
	return rc;
}
C2_EXPORTED(c2_cobfid_map_container_enum);


/*
 *****************************************************************************
 Enumerate map
 *****************************************************************************
 */

static int enum_fetch(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	return 0;
}

static bool enum_at_end(struct c2_cobfid_map_iter *iter,
				  unsigned int idx)
{
	return true;
}

static int enum_reload(struct c2_cobfid_map_iter *iter)
{
	return 0;
}

static const struct c2_cobfid_map_iter_ops enum_ops = {
	.cfmio_fetch  = enum_fetch,
	.cfmio_at_end = enum_at_end,
	.cfmio_reload = enum_reload
};

int c2_cobfid_map_enum(struct c2_cobfid_map *cfm,
		       struct c2_cobfid_map_iter *iter)
{
	int rc;

	rc = cobfid_map_iter_init(cfm, iter, &enum_ops);
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
