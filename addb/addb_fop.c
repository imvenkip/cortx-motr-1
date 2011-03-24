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

#include "fop/fop_format_def.h"

#ifdef __KERNEL__
# include "addb_k.h"
# define c2_addb_handler NULL
#else

int c2_addb_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

# include "addb_u.h"
#endif

#include "addb/addb.ff"



static struct c2_fop_type_ops addb_ops = {
	.fto_execute = c2_addb_handler,
};

C2_FOP_TYPE_DECLARE(c2_addb_record, "addb",       14, &addb_ops);
C2_FOP_TYPE_DECLARE(c2_addb_reply,  "addb reply", 0,  NULL);

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



#ifndef __KERNEL__

static void c2_addb_record_dump(const struct c2_addb_record *rec)
{
	const struct c2_addb_record_header *header = &rec->ar_header;
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
	struct c2_time now;

	c2_time_now(&now);
	header->arh_magic1    = ADDB_REC_HEADER_MAGIC1;
	header->arh_version   = ADDB_REC_HEADER_VERSION;
	header->arh_len       = size;
	header->arh_event_id  = dp->ad_ev->ae_id;
	header->arh_timestamp = c2_time_flatten(&now);
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
		strcpy(body->msg, dp->ad_name);
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


int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_stob *stob)
{
	const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct c2_addb_record        rec;
	int rc;

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
#ifndef __KERNEL__
		/*
		   XXX Write to file just for an example in DLD phase.
		   Writing into stob file should be implemented in code phase.
		 */
		FILE * log_file = (FILE*)stob;

		/* apend this record into the stob */
		fwrite(&rec.ar_header, sizeof (struct c2_addb_record_header),
			1, log_file);
		fwrite(rec.ar_data.cmb_value, rec.ar_data.cmb_count,
			1, log_file);
#endif
	}
	c2_free(rec.ar_data.cmb_value);
	return rc;
}
C2_EXPORTED(c2_addb_stob_add);

int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_table *table)
{
	/*
	 TODO store this addb record into db.
	 */
	return 0;
}
C2_EXPORTED(c2_addb_db_add);

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
C2_EXPORTED(c2_addb_net_add);



/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
