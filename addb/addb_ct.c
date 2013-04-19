/* -*- C -*- */
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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 09/25/2012
 */

/* This file is designed to be included by addb/addb.c */

/**
   @ingroup addb_pvt
   @{
 */

/* Registered context types are stored in a hash table */
enum {
	ADDB_CT_HASH_BUCKETS = 61, /**< Buckets in context type hash table */
};
M0_TL_DESCR_DEFINE(addb_ct, "ct list", static,
		   struct m0_addb_ctx_type, act_linkage, act_magic,
		   M0_ADDB_CT_MAGIC, M0_ADDB_CT_HEAD_MAGIC);
M0_TL_DEFINE(addb_ct, static, struct m0_addb_ctx_type);

/** The context type hash table */
static struct m0_tl addb_ct_htab[ADDB_CT_HASH_BUCKETS];

static uint32_t addb_ct_max_id; /* for the UT */

/** Initialize the context type hash */
static void addb_ct_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(addb_ct_htab); ++i)
		addb_ct_tlist_init(&addb_ct_htab[i]);
	m0_addb_ctx_type_register(&m0_addb_ct_node_hi);
	m0_addb_ctx_type_register(&m0_addb_ct_node_lo);
	m0_addb_ctx_type_register(&m0_addb_ct_kmod);
	m0_addb_ctx_type_register(&m0_addb_ct_process);
	m0_addb_ctx_type_register(&m0_addb_ct_ut_service);
	m0_addb_ctx_type_register(&m0_addb_ct_addb_service);
	m0_addb_ctx_type_register(&m0_addb_ct_addb_pfom);
	m0_addb_ctx_type_register(&m0_addb_ct_addb_fom);
}

/** Finalize the context type hash */
static void addb_ct_fini(void)
{
	/* This is a no-op as there is no de-registration */
}

/** Context type hash function */
static uint32_t addb_ct_hash(uint32_t id)
{
	return id % ADDB_CT_HASH_BUCKETS;
}

static struct m0_addb_ctx_type *addb_ctx_type_lookup(uint32_t id)
{
	struct m0_addb_ctx_type *ct;

	m0_tl_for(addb_ct, &addb_ct_htab[addb_ct_hash(id)], ct) {
		if (ct->act_id == id)
			return ct;
	} m0_tl_endfor;

	return NULL;
}

static bool addb_ctx_type_invariant(const struct m0_addb_ctx_type *ct)
{
	return  ct->act_magic == M0_ADDB_CT_MAGIC &&
		ct->act_name != NULL &&
		ct->act_id > 0 &&
		addb_ctx_type_lookup(ct->act_id) == ct;
}

/** @} end group addb_pvt */

/* Public interfaces */

/* pre-defined context types */
M0_ADDB_CT(m0_addb_ct_node_hi, M0_ADDB_CTXID_NODE_HI);
M0_ADDB_CT(m0_addb_ct_node_lo, M0_ADDB_CTXID_NODE_LO);
M0_ADDB_CT(m0_addb_ct_kmod,    M0_ADDB_CTXID_KMOD,    "ts");
M0_ADDB_CT(m0_addb_ct_process, M0_ADDB_CTXID_PROCESS, "ts", "procid");
M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE,   "hi", "low");
M0_ADDB_CT(m0_addb_ct_addb_service, M0_ADDB_CTXID_ADDB_SERVICE, "hi", "low");
M0_ADDB_CT(m0_addb_ct_addb_pfom, M0_ADDB_CTXID_ADDB_PFOM);
M0_ADDB_CT(m0_addb_ct_addb_fom, M0_ADDB_CTXID_ADDB_FOM);

M0_INTERNAL void m0_addb_ctx_type_register(struct m0_addb_ctx_type *ct)
{
	M0_LOG(M0_DEBUG, "name=%s", ct->act_name);

	m0_mutex_lock(&addb_mutex);

	M0_PRE(ct->act_magic == 0);
	M0_PRE(ct->act_name != NULL);
	M0_PRE(ct->act_id > 0);
	M0_PRE(addb_ctx_type_lookup(ct->act_id) == NULL);

	addb_ct_tlink_init(ct);
	addb_ct_tlist_add_tail(&addb_ct_htab[addb_ct_hash(ct->act_id)], ct);

	if (ct->act_id > addb_ct_max_id)
		addb_ct_max_id = ct->act_id; /* for the UT */

	M0_POST(addb_ctx_type_invariant(ct));

	m0_mutex_unlock(&addb_mutex);
}

M0_INTERNAL const struct m0_addb_ctx_type *m0_addb_ctx_type_lookup(uint32_t id)
{
	struct m0_addb_ctx_type *ct;

	m0_mutex_lock(&addb_mutex);
	ct = addb_ctx_type_lookup(id);
	m0_mutex_unlock(&addb_mutex);

	return ct;
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

