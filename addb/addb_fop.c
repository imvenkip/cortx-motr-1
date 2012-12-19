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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 06/19/2010
 */

#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "net/net.h"
#include "addb/addb.h"
#include "addb/addbff/addb.h"
#include "addb/addbff/addb_xc.h"
#include "mero/magic.h"
#include "rpc/rpc_opcodes.h"

static struct m0_fop_type_ops addb_ops = {
};

struct m0_fop_type m0_addb_record_fopt;
struct m0_fop_type m0_addb_reply_fopt;

M0_INTERNAL int m0_addb_fop_init(void)
{
	m0_xc_addb_init();

	return  M0_FOP_TYPE_INIT(&m0_addb_record_fopt,
				 .name      = "addb record",
				 .opcode    = M0_ADDB_RECORD_REQUEST_OPCODE,
				 .xt        = m0_addb_record_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
				 .fop_ops   = &addb_ops) ?:
		M0_FOP_TYPE_INIT(&m0_addb_reply_fopt,
				 .name      = "addb reply",
				 .opcode    = M0_ADDB_REPLY_OPCODE,
				 .xt        = m0_addb_reply_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}
M0_EXPORTED(m0_addb_fop_init);

M0_INTERNAL void m0_addb_fop_fini(void)
{
	m0_fop_type_fini(&m0_addb_record_fopt);
	m0_fop_type_fini(&m0_addb_reply_fopt);
	m0_xc_addb_fini();
}
M0_EXPORTED(m0_addb_fop_fini);

/**
   ADDB record body for function fail event.

   This event includes a message and a return value.
*/
struct m0_addb_func_fail_body {
	uint32_t rc;
	char     msg[0];
};

/**
   ADDB record body for call event.

   This event includes a return value.
*/
struct m0_addb_call_body {
	uint32_t rc;
};

/**
   ADDB record body for flag event.

   This event includes a return value.
*/
struct m0_addb_flag_body {
	bool flag;
};

/**
   ADDB record body for invalid event.

   This event includes a errno number.
*/
struct m0_addb_inval_body {
	uint64_t invalid;
};

/**
    ADDB record body for trace message.

*/
struct m0_addb_trace_body {
	char     msg[0];
};

M0_INTERNAL int m0_addb_record_header_pack(struct m0_addb_dp *dp,
					   struct m0_addb_record_header *header,
					   int size)
{
	header->arh_magic1    = M0_ADDB_REC_HEADER_MAGIC1;
	header->arh_version   = ADDB_REC_HEADER_VERSION;
	header->arh_len       = size;
	header->arh_event_id  = dp->ad_ev->ae_id;
	header->arh_timestamp = m0_time_now();
	header->arh_magic2    = M0_ADDB_REC_HEADER_MAGIC2;

	return 0;
};

/** get size for data point opaque data */
M0_INTERNAL int m0_addb_func_fail_getsize(struct m0_addb_dp *dp)
{
	return m0_align(sizeof(uint32_t) + strlen(dp->ad_name) + 1,
			M0_ADDB_RECORD_LEN_ALIGN);
}

M0_INTERNAL int m0_addb_call_getsize(struct m0_addb_dp *dp)
{
	return m0_align(sizeof(uint32_t), M0_ADDB_RECORD_LEN_ALIGN);
}
M0_INTERNAL int m0_addb_flag_getsize(struct m0_addb_dp *dp)
{
	return m0_align(sizeof(bool), M0_ADDB_RECORD_LEN_ALIGN);
}

M0_INTERNAL int m0_addb_inval_getsize(struct m0_addb_dp *dp)
{
	return m0_align(sizeof(uint64_t), M0_ADDB_RECORD_LEN_ALIGN);
}

M0_INTERNAL int m0_addb_empty_getsize(struct m0_addb_dp *dp)
{
	return 0;
}

M0_INTERNAL int m0_addb_trace_getsize(struct m0_addb_dp *dp)
{
	return m0_align(strlen(dp->ad_name) + 1, 8);
}

/** packing func fail addb record */
M0_INTERNAL int m0_addb_func_fail_pack(struct m0_addb_dp *dp,
				       struct m0_addb_record *rec)
{
	struct m0_addb_record_header  *header = &rec->ar_header;
	struct m0_addb_func_fail_body *body;
	int rc;

	M0_ASSERT(m0_addb_func_fail_getsize(dp) == rec->ar_data.cmb_count);

	rc = m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct m0_addb_func_fail_body *)rec->ar_data.cmb_value;

		M0_ASSERT(body != NULL);
		body->rc = (uint32_t)dp->ad_rc;

		strncpy(body->msg, dp->ad_name,
			rec->ar_data.cmb_count - sizeof(body->rc));
	}
	return rc;
}

M0_INTERNAL int m0_addb_call_pack(struct m0_addb_dp *dp,
				  struct m0_addb_record *rec)
{
	struct m0_addb_record_header *header = &rec->ar_header;
	struct m0_addb_call_body     *body;
	int rc;

	M0_ASSERT(m0_addb_call_getsize(dp) == rec->ar_data.cmb_count);

	rc = m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct m0_addb_call_body *)rec->ar_data.cmb_value;

		M0_ASSERT(body != NULL);
		body->rc = (uint32_t)dp->ad_rc;
	}
	return rc;
}

M0_INTERNAL int m0_addb_flag_pack(struct m0_addb_dp *dp,
				  struct m0_addb_record *rec)
{
	struct m0_addb_record_header *header = &rec->ar_header;
	struct m0_addb_flag_body     *body;
	int rc;

	M0_ASSERT(m0_addb_flag_getsize(dp) == rec->ar_data.cmb_count);

	rc = m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct m0_addb_flag_body *)rec->ar_data.cmb_value;

		M0_ASSERT(body != NULL);
		body->flag = (bool)dp->ad_rc;
	}
	return rc;
}

M0_INTERNAL int m0_addb_inval_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec)
{
	struct m0_addb_record_header *header = &rec->ar_header;
	struct m0_addb_inval_body    *body;
	int rc;

	M0_ASSERT(m0_addb_flag_getsize(dp) == rec->ar_data.cmb_count);

	rc = m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct m0_addb_inval_body *)rec->ar_data.cmb_value;

		M0_ASSERT(body != NULL);
		body->invalid = (uint64_t)dp->ad_rc;
	}
	return rc;

}

M0_INTERNAL int m0_addb_empty_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec)
{
	struct m0_addb_record_header *header = &rec->ar_header;

	M0_ASSERT(rec->ar_data.cmb_count = 0);

	return m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
}

M0_INTERNAL int m0_addb_trace_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec)
{
	struct m0_addb_record_header	*header = &rec->ar_header;
	struct m0_addb_trace_body	*body;
	int				 rc;

	M0_ASSERT(m0_addb_trace_getsize(dp) == rec->ar_data.cmb_count);

	rc = m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct m0_addb_trace_body *)rec->ar_data.cmb_value;

		M0_ASSERT(body != NULL);
		strncpy(body->msg, dp->ad_name, rec->ar_data.cmb_count);
	}
	return rc;
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
