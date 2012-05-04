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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "net/net.h"
#include "stob/stob.h"
#include "db/db.h"
#include "addb/addb.h"

#ifdef __KERNEL__
# include "addb/addbff/addb_k.h"
#else
# include "addb/addbff/addb_u.h"
#endif

#ifndef __KERNEL__

c2_bindex_t addb_stob_offset = 0;

int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_dtx *tx,
		     struct c2_stob *stob)
{
	const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct c2_addb_record        rec;
	uint32_t    bshift;
	uint64_t    bmask;
	void        *addr[2];
	c2_bcount_t count[2];
	c2_bindex_t offset[2];
	int      rc;

	if (ops->aeo_pack == NULL)
		return 0;

	C2_SET0(&rec);
	/* get size */
	rec.ar_data.cmb_count = ops->aeo_getsize(dp);
	if (rec.ar_data.cmb_count != 0) {
		rec.ar_data.cmb_value = c2_alloc(rec.ar_data.cmb_count);
		if (rec.ar_data.cmb_value == NULL)
			return -ENOMEM;
	}
	/* packing */
	rc = ops->aeo_pack(dp, &rec);
	if (rc == 0) {
		/* use stob io routines to write the addb */
		struct c2_stob_io io;
		struct c2_clink   clink;

		bshift = stob->so_op->sop_block_shift(stob);
		bmask  = (1 << bshift) - 1;

		C2_ASSERT(((sizeof rec.ar_header) & bmask) == 0);
		C2_ASSERT((rec.ar_header.arh_len & bmask) == 0);
		C2_ASSERT((addb_stob_offset & bmask) == 0);

		addr[0]   = c2_stob_addr_pack(&rec.ar_header, bshift);
		count[0]  = (sizeof rec.ar_header) >> bshift;
		offset[0] =  addb_stob_offset >> bshift;

		c2_stob_io_init(&io);

		io.si_user.ov_vec.v_nr    = 1;
		io.si_user.ov_vec.v_count = count;
		io.si_user.ov_buf         = addr;

		io.si_stob.iv_vec.v_nr    = 1;
		io.si_stob.iv_vec.v_count = count;
		io.si_stob.iv_index       = offset;

		if (rec.ar_data.cmb_count != 0) {
			char *   event_data = rec.ar_data.cmb_value;
			uint32_t data_len   = rec.ar_data.cmb_count;

			addr[1]   = c2_stob_addr_pack(event_data, bshift);
			count[1]  = data_len >> bshift;
			offset[1] = (offset[0] + count[0]) >> bshift;
			io.si_user.ov_vec.v_nr = 2;
			io.si_stob.iv_vec.v_nr = 2;
		}
		io.si_opcode = SIO_WRITE;
		io.si_flags  = 0;

		c2_clink_init(&clink, NULL);
		c2_clink_add(&io.si_wait, &clink);

		rc = c2_stob_io_launch(&io, stob, tx, NULL);

		if (rc == 0)
			c2_chan_wait(&clink);

		if (rc == 0 && io.si_rc == 0)
			addb_stob_offset += io.si_count << bshift;

		c2_clink_del(&clink);
		c2_clink_fini(&clink);

		c2_stob_io_fini(&io);
	}
	c2_free(rec.ar_data.cmb_value);
	return rc;
}

uint64_t c2_addb_db_seq = 0;

int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_dbenv *env,
		   struct c2_table *table)
{
	const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct c2_addb_record rec;
	struct c2_db_pair     pair;
	uint32_t	      keysize;
	uint32_t	      recsize;
	char 		      *data;
	struct c2_db_tx	       tx;
	int      rc;

	if (ops->aeo_pack == NULL)
		return 0;

	C2_SET0(&rec);
	/* get size */
	rec.ar_data.cmb_count = ops->aeo_getsize(dp);
	if (rec.ar_data.cmb_count != 0) {
		rec.ar_data.cmb_value = c2_alloc(rec.ar_data.cmb_count);
		if (rec.ar_data.cmb_value == NULL)
			return -ENOMEM;
	}
	/* packing */
	rc = ops->aeo_pack(dp, &rec);
	if (rc == 0) {
		/* use db routines to write the addb */
		keysize = sizeof c2_addb_db_seq;
		recsize = sizeof (struct c2_addb_record_header) + rec.ar_data.cmb_count;

		data = c2_alloc(recsize);
		if (data == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		rc = c2_db_tx_init(&tx, env, 0);

		memcpy(data, &rec.ar_header, sizeof (rec.ar_header));
		memcpy(data + sizeof (rec.ar_header), rec.ar_data.cmb_value,
			rec.ar_data.cmb_count);
		++c2_addb_db_seq;
		c2_db_pair_setup(&pair, table,
				 &c2_addb_db_seq, keysize,
				 data, recsize);
		rc = c2_table_insert(&tx, &pair);
		c2_db_pair_fini(&pair);

		c2_free(data);

		c2_db_tx_commit(&tx);
	}
out:
	c2_free(rec.ar_data.cmb_value);
	return rc;
}
#else

int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_dtx *tx,
		     struct c2_stob *stob)
{
	return 0;
}
int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_dbenv *env,
		   struct c2_table *table)
{
	return 0;
}

#endif

int c2_addb_net_add(struct c2_addb_dp *dp, struct c2_net_conn *conn)
{
	const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct c2_fop         *request;
	struct c2_fop         *reply;
	struct c2_addb_record *addb_record;
	struct c2_addb_reply  *addb_reply;
	struct c2_net_call    call;
	int size;
	int result;

	if (ops->aeo_pack == NULL)
		return 0;

	request = c2_fop_alloc(&c2_addb_record_fopt, NULL);
	reply   = c2_fop_alloc(&c2_addb_reply_fopt, NULL);
	if (request == NULL || reply == NULL) {
		result = -ENOMEM;
		goto out;
	}

	addb_record = c2_fop_data(request);
	addb_reply  = c2_fop_data(reply);

	/* get size */
	size = ops->aeo_getsize(dp);
	if (size != 0) {
		addb_record->ar_data.cmb_value = c2_alloc(size);
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
		C2_SET0(addb_reply);
		call.ac_arg = request;
		call.ac_ret = reply;
		result = c2_net_cli_call(conn, &call);
		/* call c2_rpc_item_submit() in the future */
	}
	c2_free(addb_record->ar_data.cmb_value);
out:
	c2_fop_free(request);
	c2_fop_free(reply);

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
