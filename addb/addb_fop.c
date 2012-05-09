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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 06/19/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/cdefs.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "net/net.h"
#include "addb/addb.h"

#ifdef __KERNEL__
# include "addb/addb_k.h"
# define c2_addb_handler NULL
#else

int c2_addb_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

# include "addb/addb_u.h"
#endif

#include "fop/fop_format_def.h"
#include "addb/addb.ff"
#include "rpc/rpc_opcodes.h"

static struct c2_fop_type_ops addb_ops = {
	.fto_execute = c2_addb_handler,
};

C2_FOP_TYPE_DECLARE(c2_addb_record, "addb", &addb_ops,
		    C2_ADDB_RECORD_REQUEST_OPCODE, C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE(c2_addb_reply,  "addb reply", NULL, C2_ADDB_REPLY_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);
/**
   ADDB record body for function fail event.

   This event includes a message and a return value.
*/
struct c2_addb_func_fail_body {
	uint32_t rc;
	char     msg[0];
};

/**
   ADDB record body for call event.

   This event includes a return value.
*/
struct c2_addb_call_body {
	uint32_t rc;
};

/**
   ADDB record body for flag event.

   This event includes a return value.
*/
struct c2_addb_flag_body {
	bool flag;
};

/**
   ADDB record body for invalid event.

   This event includes a errno number.
*/
struct c2_addb_inval_body {
	uint64_t invalid;
};

/**
    ADDB record body for trace message.

*/
struct c2_addb_trace_body {
	char     msg[0];
};

#ifndef __KERNEL__
static int c2_addb_enable_dump = 0;

static void c2_addb_record_dump(const struct c2_addb_record *rec)
{
	const struct c2_addb_record_header *header = &rec->ar_header;

	if (c2_addb_enable_dump == 0)
		return;

	printf("addb record |- magic1    = %llX\n"
	       "            |- version   = %lu\n"
	       "            |- len       = %lu\n"
	       "            |- event_id  = %llu\n"
	       "            |- timestamp = %llx\n"
	       "            |- magic2    = %llX\n"
	       "            |- opaque data length = %lu\n",
	       (unsigned long long)header->arh_magic1,
	       (unsigned long)     header->arh_version,
	       (unsigned long)     header->arh_len,
	       (unsigned long long)header->arh_event_id,
	       (unsigned long long)header->arh_timestamp,
	       (unsigned long long)header->arh_magic2,
	       (unsigned long)     rec->ar_data.cmb_count);

	switch (header->arh_event_id) {
	case C2_ADDB_EVENT_FUNC_FAIL: {
		const struct c2_addb_func_fail_body *body;
		body = (struct c2_addb_func_fail_body*) rec->ar_data.cmb_value;

		printf("++func-fail++> rc = %d, msg = %s\n", body->rc, body->msg);
		break;
		}
	default:
		break;
	}
}

int c2_addb_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_addb_record   *in;
	struct c2_addb_reply    *ex;
	struct c2_fop           *reply;

	in = c2_fop_data(fop);
	/* do something with the request, e.g. store it in stob, or in db */
	c2_addb_record_dump(in);

	/* prepare the reply */
	reply = c2_fop_alloc(&c2_addb_reply_fopt, NULL);
	if (reply != NULL) {
		ex = c2_fop_data(reply);
		ex->ar_rc = 0;
	}

	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 1;
}
#endif

static int c2_addb_record_header_pack(struct c2_addb_dp *dp,
				      struct c2_addb_record_header *header,
				      int size)
{
	header->arh_magic1    = ADDB_REC_HEADER_MAGIC1;
	header->arh_version   = ADDB_REC_HEADER_VERSION;
	header->arh_len       = size;
	header->arh_event_id  = dp->ad_ev->ae_id;
	header->arh_timestamp = c2_time_now();
	header->arh_magic2    = ADDB_REC_HEADER_MAGIC2;

	return 0;
};

/** get size for data point opaque data */
int c2_addb_func_fail_getsize(struct c2_addb_dp *dp)
{
	return c2_align(sizeof(uint32_t) + strlen(dp->ad_name) + 1, 8);
}

int c2_addb_call_getsize(struct c2_addb_dp *dp)
{
	return c2_align(sizeof(uint32_t), 8);
}
int c2_addb_flag_getsize(struct c2_addb_dp *dp)
{
	return c2_align(sizeof(bool), 8);
}

int c2_addb_inval_getsize(struct c2_addb_dp *dp)
{
	return c2_align(sizeof(uint64_t), 8);
}

int c2_addb_empty_getsize(struct c2_addb_dp *dp)
{
	return 0;
}

int c2_addb_trace_getsize(struct c2_addb_dp *dp)
{
	return c2_align(strlen(dp->ad_name) + 1, 8);
}

/** packing func fail addb record */
int c2_addb_func_fail_pack(struct c2_addb_dp *dp,
			   struct c2_addb_record *rec)
{
	struct c2_addb_record_header  *header = &rec->ar_header;
	struct c2_addb_func_fail_body *body;
	int rc;

	C2_ASSERT(c2_addb_func_fail_getsize(dp) == rec->ar_data.cmb_count);

	rc = c2_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct c2_addb_func_fail_body *)rec->ar_data.cmb_value;

		C2_ASSERT(body != NULL);
		body->rc = (uint32_t)dp->ad_rc;

		strncpy(body->msg, dp->ad_name,
			rec->ar_data.cmb_count - sizeof(body->rc));
	}
	return rc;
}

int c2_addb_call_pack(struct c2_addb_dp *dp,
		      struct c2_addb_record *rec)
{
	struct c2_addb_record_header *header = &rec->ar_header;
	struct c2_addb_call_body     *body;
	int rc;

	C2_ASSERT(c2_addb_call_getsize(dp) == rec->ar_data.cmb_count);

	rc = c2_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct c2_addb_call_body *)rec->ar_data.cmb_value;

		C2_ASSERT(body != NULL);
		body->rc = (uint32_t)dp->ad_rc;
	}
	return rc;
}

int c2_addb_flag_pack(struct c2_addb_dp *dp,
		      struct c2_addb_record *rec)
{
	struct c2_addb_record_header *header = &rec->ar_header;
	struct c2_addb_flag_body     *body;
	int rc;

	C2_ASSERT(c2_addb_flag_getsize(dp) == rec->ar_data.cmb_count);

	rc = c2_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct c2_addb_flag_body *)rec->ar_data.cmb_value;

		C2_ASSERT(body != NULL);
		body->flag = (bool)dp->ad_rc;
	}
	return rc;
}

int c2_addb_inval_pack(struct c2_addb_dp *dp,
		       struct c2_addb_record *rec)
{
	struct c2_addb_record_header *header = &rec->ar_header;
	struct c2_addb_inval_body    *body;
	int rc;

	C2_ASSERT(c2_addb_flag_getsize(dp) == rec->ar_data.cmb_count);

	rc = c2_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct c2_addb_inval_body *)rec->ar_data.cmb_value;

		C2_ASSERT(body != NULL);
		body->invalid = (uint64_t)dp->ad_rc;
	}
	return rc;

}

int c2_addb_empty_pack(struct c2_addb_dp *dp,
		       struct c2_addb_record *rec)
{
	struct c2_addb_record_header *header = &rec->ar_header;

	C2_ASSERT(rec->ar_data.cmb_count = 0);

	return c2_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
}

int c2_addb_trace_pack(struct c2_addb_dp *dp,
		       struct c2_addb_record *rec)
{
	struct c2_addb_record_header	*header = &rec->ar_header;
	struct c2_addb_trace_body	*body;
	int				 rc;

	C2_ASSERT(c2_addb_trace_getsize(dp) == rec->ar_data.cmb_count);

	rc = c2_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0 && rec->ar_data.cmb_count > 0) {
		body = (struct c2_addb_trace_body *)rec->ar_data.cmb_value;

		C2_ASSERT(body != NULL);
		strncpy(body->msg, dp->ad_name, rec->ar_data.cmb_count);
	}
	return rc;
}

extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U32_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U64_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_BYTE_tfmt;
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
