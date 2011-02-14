/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/arith.h" /* max_check */
#include "lib/memory.h"/*c2_alloc/c2_free */
#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/errno.h" /* errno */
#include "lib/misc.h"
#include "lib/rwlock.h"
#include "stob/stob.h"
#include "db/db.h"
#include "net/net.h"

#include "addb/addb.h"

/**
   @addtogroup addb

   @todo check for recursive invocations.

   @{
 */

/*
 * This can be changed.
 */
int default_addb_level = AEL_ERROR;
C2_EXPORTED(default_addb_level);


int c2_addb_init(void)
{
	return 0;
}
C2_EXPORTED(c2_addb_init);

void c2_addb_fini(void)
{
}
C2_EXPORTED(c2_addb_fini);

void c2_addb_ctx_init(struct c2_addb_ctx *ctx, const struct c2_addb_ctx_type *t,
		      struct c2_addb_ctx *parent)
{
	ctx->ac_type = t;
	ctx->ac_parent = parent;
}
C2_EXPORTED(c2_addb_ctx_init);

void c2_addb_ctx_fini(struct c2_addb_ctx *ctx)
{
}
C2_EXPORTED(c2_addb_ctx_fini);

/* defined in {,linux_kernel/}addb_console.c */
void c2_addb_console(enum c2_addb_ev_level lev, struct c2_addb_dp *dp);

void c2_addb_add(struct c2_addb_dp *dp)
{
	enum c2_addb_ev_level     lev;
	const struct c2_addb_ev  *ev;

	ev  = dp->ad_ev;
	lev = max_check(dp->ad_level, max_check(ev->ae_level,
						ev->ae_ops->aeo_level));
	/* log high priority data points to the console */
	if (lev >= AEL_NOTE)
		c2_addb_console(lev, dp);

	switch (c2_addb_store_type) {
	case C2_ADDB_REC_STORE_STOB:
		C2_ASSERT(c2_addb_store_stob != NULL);
		c2_addb_stob_add(dp, c2_addb_store_stob);
		break;
	case C2_ADDB_REC_STORE_DB:
		C2_ASSERT(c2_addb_store_table != NULL);
		c2_addb_db_add(dp, c2_addb_store_table);
		break;
	case C2_ADDB_REC_STORE_NETWORK:
		C2_ASSERT(c2_addb_store_net_domain != NULL);
		c2_addb_net_add(dp, c2_addb_store_net_domain);
		break;
	default:
		C2_ASSERT(c2_addb_store_type == C2_ADDB_REC_STORE_NONE);
		break;
	}
}
C2_EXPORTED(c2_addb_add);

static int subst_name_int(struct c2_addb_dp *dp, const char *name, int rc)
{
	dp->ad_name = name;
	dp->ad_rc = rc;
	return 0;
}

static int subst_int(struct c2_addb_dp *dp, int rc)
{
	dp->ad_name = "";
	dp->ad_rc = rc;
	return 0;
}

static int subst_void(struct c2_addb_dp *dp)
{
	dp->ad_name = "";
	return 0;
}

static int subst_uint64_t(struct c2_addb_dp *dp, uint64_t val)
{
	dp->ad_name = "";
	dp->ad_rc = val;
	return 0;
}

static int c2_addb_rec_header_pack(struct c2_addb_dp *dp,
				   struct c2_buf *buf)
{
	struct c2_time now;
	struct c2_addb_rec_header *header;
	header = (struct c2_addb_rec_header *)buf->b_addr;

	header->arh_magic1   = ADDB_REC_HEADER_MAGIC1;
	header->arh_version  = ADDB_REC_HEADER_VERSION;
	header->arh_len      = buf->b_nob;
	header->arh_event_id = dp->ad_ev->ae_id;
	c2_time_now(&now);
	header->arh_timestamp = c2_time_flatten(&now);
	header->arh_magic2   = ADDB_REC_HEADER_MAGIC2;

	return 0;
};
const struct c2_addb_ev_ops C2_ADDB_SYSCALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "syscall-failure",
	.aeo_level = AEL_NOTE
};
C2_EXPORTED(C2_ADDB_SYSCALL);

/**
   ADDB record body for function fail event.

   This event includes a message and a return value.
*/
struct c2_addb_func_fail_body {
	uint32_t rc;
	char     msg[0];
};

static int c2_addb_func_fail_pack(struct c2_addb_dp *dp, struct c2_buf *buf)
{
	struct c2_addb_rec_header *header;
	struct c2_addb_func_fail_body *body;
	int left;
	int rc;

	if (buf->b_nob == 0) {
		C2_ASSERT(buf->b_addr == NULL);
		buf->b_nob = c2_align(sizeof(struct c2_addb_rec_header) +
				      sizeof(uint32_t) + strlen(dp->ad_name) +1,
				      8);
		return 0;
	}
	C2_ASSERT(buf->b_addr != NULL);
	C2_ASSERT(buf->b_nob >= sizeof(struct c2_addb_rec_header));

	rc = c2_addb_rec_header_pack(dp, buf);
	if (rc == 0) {
		header = (struct c2_addb_rec_header *)buf->b_addr;
		body = (struct c2_addb_func_fail_body *)header->arh_body;
		left = buf->b_nob - sizeof(struct c2_addb_rec_header) -
			   sizeof(uint32_t);

		if (left >= 0) {
			body->rc = dp->ad_rc;
			strncpy(body->msg, dp->ad_name, left);
		}
	}
	return rc;
}


const struct c2_addb_ev_ops C2_ADDB_FUNC_CALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_name_int,
	.aeo_pack  = c2_addb_func_fail_pack,
	.aeo_size  = sizeof(int32_t) + sizeof(char *),
	.aeo_name  = "function-failure",
	.aeo_level = AEL_NOTE
};
C2_EXPORTED(C2_ADDB_FUNC_CALL);

const struct c2_addb_ev_ops C2_ADDB_CALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "call-failure",
	.aeo_level = AEL_NOTE
};
C2_EXPORTED(C2_ADDB_CALL);

const struct c2_addb_ev_ops C2_ADDB_STAMP = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_void,
	.aeo_size  = 0,
	.aeo_name  = "."
};
C2_EXPORTED(C2_ADDB_STAMP);

const struct c2_addb_ev_ops C2_ADDB_FLAG = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_void,
	.aeo_size  = sizeof(bool),
	.aeo_name  = "flag"
};
C2_EXPORTED(C2_ADDB_FLAG);

const struct c2_addb_ev_ops C2_ADDB_INVAL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_uint64_t,
	.aeo_size  = sizeof(uint64_t),
	.aeo_name  = "inval"
};
C2_EXPORTED(C2_ADDB_INVAL);

struct c2_addb_ev c2_addb_oom = {
	.ae_name = "oom",
	.ae_id   = 0x3,
	.ae_ops  = &C2_ADDB_STAMP
};
C2_EXPORTED(c2_addb_oom);

struct c2_addb_ev c2_addb_func_fail = {
	.ae_name = "func-fail",
	.ae_id   = 0x4,
	.ae_ops  = &C2_ADDB_FUNC_CALL
};
C2_EXPORTED(c2_addb_func_fail);

static const struct c2_addb_ctx_type c2_addb_global_ctx_type = {
	.act_name = "global"
};
C2_EXPORTED(c2_addb_global_ctx_type);

struct c2_addb_ctx c2_addb_global_ctx = {
	.ac_type   = &c2_addb_global_ctx_type,
	.ac_parent = NULL
};
C2_EXPORTED(c2_addb_global_ctx);

enum c2_addb_rec_store_type c2_addb_store_type       = C2_ADDB_REC_STORE_NONE;
struct c2_stob             *c2_addb_store_stob       = NULL;
struct c2_table            *c2_addb_store_table      = NULL;
struct c2_net_domain       *c2_addb_store_net_domain = NULL;

int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_stob *stob)
{
	struct c2_buf buf = { NULL, 0};
	int rc;

	if (dp->ad_ev->ae_ops->aeo_pack == NULL)
		return 0;

	/* get size */
	rc = dp->ad_ev->ae_ops->aeo_pack(dp, &buf);
	if (rc != 0)
		return rc;

	buf.b_addr = c2_alloc(buf.b_nob);
	if (buf.b_addr == NULL)
		return -ENOMEM;

	/* real packing */
	rc = dp->ad_ev->ae_ops->aeo_pack(dp, &buf);
	if (rc == 0) {
		/* use stob io routines to write the addb */
#ifndef __KERNEL__
		/* XXX write to file just for an example. */
		FILE * log_file = (FILE*)stob;

		/* apend this record into the stob */
		fwrite(buf.b_addr, buf.b_nob, 1, log_file);
#endif
	}
	c2_free(buf.b_addr);
	return rc;
}
C2_EXPORTED(c2_addb_stob_add);

int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_table *table)
{
	return 0;
}
C2_EXPORTED(c2_addb_db_add);


int c2_addb_net_add(struct c2_addb_dp *dp, struct c2_net_domain *dom)
{
	struct c2_buf buf = { NULL, 0};
	struct c2_addb_rec_item *item;
	int rc;

	if (dp->ad_ev->ae_ops->aeo_pack == NULL) 
		return 0;

	item = c2_alloc(sizeof *item);
	if (item == NULL)
		return -ENOMEM;

	c2_list_link_init(&item->ari_linkage);

	/* get size */
	rc = dp->ad_ev->ae_ops->aeo_pack(dp, &buf);
	if (rc != 0) {
		c2_free(item);
		return rc;
	}

	buf.b_addr = c2_alloc(buf.b_nob);
	if (buf.b_addr == NULL) {
		c2_free(item);
		return -ENOMEM;
	}

	/* item & buf will be freed when this item is sent. */

	/* real packing */
	rc = dp->ad_ev->ae_ops->aeo_pack(dp, &buf);
	if (rc == 0) {
		item->ari_header = buf.b_addr;
		/* add item into a queue */
		c2_rwlock_write_lock(&dom->nd_addb_lock);
		c2_list_add(&dom->nd_addb_items,
			    &item->ari_linkage);
		c2_rwlock_write_unlock(&dom->nd_addb_lock);
	} else {
		/* error out. cleanup */
		c2_free(item);
		c2_free(buf.b_addr);
	}
	return rc;
}
C2_EXPORTED(c2_addb_net_add);


/** @} end of addb group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
