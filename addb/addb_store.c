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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 03/17/2011
 */

#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "net/net.h"
#include "stob/stob.h"
#include "db/db.h"
#include "addb/addb.h"

#ifndef __KERNEL__

m0_bindex_t addb_stob_offset = 0;

M0_INTERNAL int m0_addb_stob_add(struct m0_addb_dp *dp, struct m0_dtx *tx,
				 struct m0_stob *stob)
{
	const struct m0_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct m0_addb_record        rec;
	uint32_t    bshift;
	uint64_t    bmask;
	void        *addr[2];
	m0_bcount_t count[2];
	m0_bindex_t offset[2];
	int      rc;

	if (ops->aeo_pack == NULL)
		return 0;

	M0_SET0(&rec);
	/* get size */
	rec.ar_data.cmb_count = ops->aeo_getsize(dp);
	if (rec.ar_data.cmb_count != 0) {
		rec.ar_data.cmb_value = m0_alloc(rec.ar_data.cmb_count);
		if (rec.ar_data.cmb_value == NULL)
			return -ENOMEM;
	}
	/* packing */
	rc = ops->aeo_pack(dp, &rec);
	if (rc == 0) {
		/* use stob io routines to write the addb */
		struct m0_stob_io io;
		struct m0_clink   clink;

		bshift = stob->so_op->sop_block_shift(stob);
		bmask  = (1 << bshift) - 1;

		M0_ASSERT(((sizeof rec.ar_header) & bmask) == 0);
		M0_ASSERT((rec.ar_header.arh_len & bmask) == 0);
		M0_ASSERT((addb_stob_offset & bmask) == 0);

		addr[0]   = m0_stob_addr_pack(&rec.ar_header, bshift);
		count[0]  = (sizeof rec.ar_header) >> bshift;
		offset[0] =  addb_stob_offset >> bshift;

		m0_stob_io_init(&io);

		io.si_user.ov_vec.v_nr    = 1;
		io.si_user.ov_vec.v_count = count;
		io.si_user.ov_buf         = addr;

		io.si_stob.iv_vec.v_nr    = 1;
		io.si_stob.iv_vec.v_count = count;
		io.si_stob.iv_index       = offset;

		if (rec.ar_data.cmb_count != 0) {
			char *   event_data = (char *) rec.ar_data.cmb_value;
			uint32_t data_len   = rec.ar_data.cmb_count;

			addr[1]   = m0_stob_addr_pack(event_data, bshift);
			count[1]  = data_len >> bshift;
			offset[1] = (offset[0] + count[0]) >> bshift;
			io.si_user.ov_vec.v_nr = 2;
			io.si_stob.iv_vec.v_nr = 2;
		}
		io.si_opcode = SIO_WRITE;
		io.si_flags  = 0;

		m0_clink_init(&clink, NULL);
		m0_clink_add(&io.si_wait, &clink);

		rc = m0_stob_io_launch(&io, stob, tx, NULL);

		if (rc == 0)
			m0_chan_wait(&clink);

		if (rc == 0 && io.si_rc == 0)
			addb_stob_offset += io.si_count << bshift;

		m0_clink_del(&clink);
		m0_clink_fini(&clink);

		m0_stob_io_fini(&io);
	}
	m0_free(rec.ar_data.cmb_value);
	return rc;
}

uint64_t m0_addb_db_seq = 0;

M0_INTERNAL int m0_addb_db_add(struct m0_addb_dp *dp, struct m0_dbenv *env,
			       struct m0_table *table)
{
	const struct m0_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct m0_addb_record rec;
	struct m0_db_pair     pair;
	uint32_t	      keysize;
	uint32_t	      recsize;
	char 		      *data;
	struct m0_db_tx	       tx;
	int      rc;

	if (ops->aeo_pack == NULL)
		return 0;

	M0_SET0(&rec);
	/* get size */
	rec.ar_data.cmb_count = ops->aeo_getsize(dp);
	if (rec.ar_data.cmb_count != 0) {
		rec.ar_data.cmb_value = m0_alloc(rec.ar_data.cmb_count);
		if (rec.ar_data.cmb_value == NULL)
			return -ENOMEM;
	}
	/* packing */
	rc = ops->aeo_pack(dp, &rec);
	if (rc == 0) {
		/* use db routines to write the addb */
		keysize = sizeof m0_addb_db_seq;
		recsize = sizeof (struct m0_addb_record_header) + rec.ar_data.cmb_count;

		data = m0_alloc(recsize);
		if (data == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		rc = m0_db_tx_init(&tx, env, 0);

		memcpy(data, &rec.ar_header, sizeof (rec.ar_header));
		memcpy(data + sizeof (rec.ar_header), rec.ar_data.cmb_value,
			rec.ar_data.cmb_count);
		++m0_addb_db_seq;
		m0_db_pair_setup(&pair, table,
				 &m0_addb_db_seq, keysize,
				 data, recsize);
		rc = m0_table_insert(&tx, &pair);
		m0_db_pair_fini(&pair);

		m0_free(data);

		m0_db_tx_commit(&tx);
	}
out:
	m0_free(rec.ar_data.cmb_value);
	return rc;
}
#else

int m0_addb_stob_add(struct m0_addb_dp *dp, struct m0_dtx *tx,
		     struct m0_stob *stob)
{
	return 0;
}
M0_INTERNAL int m0_addb_db_add(struct m0_addb_dp *dp, struct m0_dbenv *env,
			       struct m0_table *table)
{
	return 0;
}

#endif

/**
   @todo Use new net interfaces here.
 */

M0_INTERNAL int m0_addb_net_add(struct m0_addb_dp *dp, struct m0_net_conn *conn)
{
	const struct m0_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct m0_fop         *request;
	struct m0_fop         *reply;
	struct m0_addb_record *addb_record;
	struct m0_addb_reply  *addb_reply;
	int size;
	int result;

	if (ops->aeo_pack == NULL)
		return 0;

	request = m0_fop_alloc(&m0_addb_record_fopt, NULL);
	reply   = m0_fop_alloc(&m0_addb_reply_fopt, NULL);
	if (request == NULL || reply == NULL) {
		result = -ENOMEM;
		goto out;
	}

	addb_record = m0_fop_data(request);
	addb_reply  = m0_fop_data(reply);

	/* get size */
	size = ops->aeo_getsize(dp);
	if (size != 0) {
		addb_record->ar_data.cmb_value = m0_alloc(size);
		if (addb_record->ar_data.cmb_value == NULL) {
			result = -ENOMEM;
			goto out;
		}
	} else
		addb_record->ar_data.cmb_value = NULL;
	addb_record->ar_data.cmb_count = size;
	/* packing */
	result = ops->aeo_pack(dp, addb_record);
	if (result == 0) {
		M0_SET0(addb_reply);
		/* @todo call m0_rpc_item_submit() in the future */
	}
	m0_free(addb_record->ar_data.cmb_value);
out:
	m0_fop_put(request);
	m0_fop_put(reply);

	return result;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
