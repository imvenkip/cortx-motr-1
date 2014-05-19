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
 * Original creation date: 09/26/2012
 */

/* This file is designed to be included by addb/addb.c */

#include "sm/sm.h"

/**
   @ingroup addb_pvt
   @{
 */

/* Registered record types are stored in a hash table */
enum {
	ADDB_RT_HASH_BUCKETS = 127, /**< Buckets in record type hash table */
};
M0_TL_DESCR_DEFINE(addb_rt, "rt list", static,
		   struct m0_addb_rec_type, art_linkage, art_magic,
		   M0_ADDB_RT_MAGIC, M0_ADDB_RT_HEAD_MAGIC);
M0_TL_DEFINE(addb_rt, static, struct m0_addb_rec_type);

/** The context type hash table */
static struct m0_tl addb_rt_htab[ADDB_RT_HASH_BUCKETS];

static uint32_t addb_rt_max_id; /* Represent highest rec type id registred */

/** Record type for context definition. */
static struct m0_addb_rec_type addb_rt_ctxdef = {
	.art_base_type = M0_ADDB_BRT_CTXDEF,
	.art_name      = "addb_rt_ctxdef",
	.art_id        = M0_ADDB_RECID_CTXDEF,
	.art_rf_nr     = 0
};

M0_ADDB_RT_EX(m0_addb_rt_func_fail,  M0_ADDB_RECID_FUNC_FAIL, "loc", "rc");
M0_ADDB_RT_EX(m0_addb_rt_oom,  M0_ADDB_RECID_OOM, "loc");
M0_ADDB_RT_EX(m0_addb_rt_resv4,  M0_ADDB_RECID_RESV4);
M0_ADDB_RT_EX(m0_addb_rt_resv5,  M0_ADDB_RECID_RESV5);
M0_ADDB_RT_EX(m0_addb_rt_resv6,  M0_ADDB_RECID_RESV6);
M0_ADDB_RT_EX(m0_addb_rt_resv7,  M0_ADDB_RECID_RESV7);
M0_ADDB_RT_EX(m0_addb_rt_resv8,  M0_ADDB_RECID_RESV8);
M0_ADDB_RT_EX(m0_addb_rt_resv9,  M0_ADDB_RECID_RESV9);
M0_ADDB_RT_EX(m0_addb_rt_resv10, M0_ADDB_RECID_RESV10);

/** Initialize the context type hash */
static void addb_rt_init(void)
{
	int i;

	for (i = 0; i < ADDB_RT_HASH_BUCKETS; ++i)
		addb_rt_tlist_init(&addb_rt_htab[i]);
	m0_addb_rec_type_register(&addb_rt_ctxdef);
	m0_addb_rec_type_register(&m0_addb_rt_func_fail);
	m0_addb_rec_type_register(&m0_addb_rt_oom);
	m0_addb_rec_type_register(&m0_addb_rt_resv4);
	m0_addb_rec_type_register(&m0_addb_rt_resv5);
	m0_addb_rec_type_register(&m0_addb_rt_resv6);
	m0_addb_rec_type_register(&m0_addb_rt_resv7);
	m0_addb_rec_type_register(&m0_addb_rt_resv8);
	m0_addb_rec_type_register(&m0_addb_rt_resv9);
	m0_addb_rec_type_register(&m0_addb_rt_resv10);
}

/** Finalize the context type hash */
static void addb_rt_fini(void)
{
	/* This is a no-op as there is no de-registration */
}

/** Context type hash function */
static uint32_t addb_rt_hash(uint32_t id)
{
	return id % ADDB_RT_HASH_BUCKETS;
}

static struct m0_addb_rec_type *addb_rec_type_lookup(uint32_t id)
{
	return m0_tl_find(addb_rt, rt, &addb_rt_htab[addb_rt_hash(id)],
			  rt->art_id == id);
}

static bool addb_rec_type_invariant(const struct m0_addb_rec_type *rt)
{
	return  rt != NULL &&
		rt->art_magic == M0_ADDB_RT_MAGIC &&
		rt->art_base_type >= M0_ADDB_BRT_EX &&
		rt->art_base_type < M0_ADDB_BRT_NR &&
		((rt->art_base_type == M0_ADDB_BRT_SM_CNTR) ==
		 (rt->art_sm_conf != NULL)) &&
		ergo(rt->art_base_type == M0_ADDB_BRT_SM_CNTR,
		     m0_sm_conf_is_initialized(rt->art_sm_conf)) &&
		rt->art_name != NULL &&
		rt->art_id > 0 &&
		ergo(rt->art_rf_nr > 0, rt->art_rf != NULL) &&
		addb_rec_type_lookup(rt->art_id) == rt;
}

/** @} end group addb_pvt */

/* Public interfaces */

M0_INTERNAL void m0_addb_rec_type_register(struct m0_addb_rec_type *rt)
{
	m0_mutex_lock(&addb_mutex);

	M0_PRE(rt->art_magic == 0);
	M0_PRE(rt->art_name != NULL);
	M0_PRE(rt->art_id > 0);
	M0_PRE(rt->art_base_type >= M0_ADDB_BRT_EX);
	M0_PRE(rt->art_base_type < M0_ADDB_BRT_NR);
	M0_PRE(addb_rec_type_lookup(rt->art_id) == NULL);

	addb_rt_tlink_init(rt);
	addb_rt_tlist_add_tail(&addb_rt_htab[addb_rt_hash(rt->art_id)], rt);

	if (rt->art_id > addb_rt_max_id)
		addb_rt_max_id = rt->art_id;

	M0_POST(addb_rec_type_invariant(rt));

	m0_mutex_unlock(&addb_mutex);
}

M0_INTERNAL const struct m0_addb_rec_type *m0_addb_rec_type_lookup(uint32_t id)
{
	struct m0_addb_rec_type *rt;

	m0_mutex_lock(&addb_mutex);
	rt = addb_rec_type_lookup(id);
	m0_mutex_unlock(&addb_mutex);

	return rt;
}

M0_INTERNAL uint32_t m0_addb_rec_type_name2id(const char *rt_name)
{
	int i;

	M0_PRE(rt_name != NULL);

	for (i = 0; i < ADDB_RT_HASH_BUCKETS; ++i) {
		struct m0_addb_rec_type *rt;
		m0_tl_for(addb_rt, &addb_rt_htab[i], rt) {
			if (strcmp(rt->art_name, rt_name) == 0)
				return rt->art_id;
		} m0_tl_endfor;
	}

	return M0_ADDB_RECID_UNDEF;
}

M0_INTERNAL uint32_t m0_addb_rec_type_max_id(void)
{
	return addb_rt_max_id;
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
