/* -*- c -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 09-Feb-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/obj_ops.h"
#include "conf/cache.h"
#include "conf/onwire.h"   /* m0_confx_obj */
#include "lib/misc.h"      /* M0_IN */
#include "lib/arith.h"     /* M0_CNT_INC, M0_CNT_DEC */
#include "lib/errno.h"     /* ENOMEM */
#include "mero/magic.h"    /* M0_CONF_OBJ_MAGIC */

/**
 * @defgroup conf_dlspec_objops Configuration Object Operations (lspec)
 *
 * @see @ref conf, @ref conf-lspec
 *
 * @{
 */

static bool _generic_obj_invariant(const void *bob);
static bool _concrete_obj_invariant(const struct m0_conf_obj *obj);

static const struct m0_bob_type generic_obj_bob = {
	.bt_name         = "m0_conf_obj",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_conf_obj, co_gen_magic),
	.bt_magix        = M0_CONF_OBJ_MAGIC,
	.bt_check        = _generic_obj_invariant
};
M0_BOB_DEFINE(static, &generic_obj_bob, m0_conf_obj);

M0_INTERNAL bool m0_conf_obj_invariant(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_bob_check(obj) && _concrete_obj_invariant(obj);
}

static bool _generic_obj_invariant(const void *bob)
{
	const struct m0_conf_obj *obj = bob;

	return 0 <= obj->co_type && obj->co_type < M0_CO_NR &&
		m0_fid_is_set(&obj->co_id) && obj->co_ops != NULL &&
		M0_IN(obj->co_status,
		      (M0_CS_MISSING, M0_CS_LOADING, M0_CS_READY)) &&
		ergo(m0_conf_obj_is_stub(obj), obj->co_nrefs == 0) &&
		ergo(obj->co_mounted, m0_conf_cache_tlist_contains(
			     &obj->co_cache->ca_registry, obj));
}

static bool _concrete_obj_invariant(const struct m0_conf_obj *obj)
{
	bool ret = obj->co_ops->coo_invariant(obj);
	if (unlikely(!ret))
		M0_LOG(M0_ERROR, "Configuration object invariant doesn't hold: "
		       "type=%d, id="FID_F, obj->co_type, FID_P(&obj->co_id));
	return ret;
}

M0_INTERNAL struct m0_conf_obj *m0_conf__dir_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__profile_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__filesystem_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__service_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__node_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__nic_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__sdev_create(void);
M0_INTERNAL struct m0_conf_obj *m0_conf__partition_create(void);

static struct m0_conf_obj *(*concrete_ctors[M0_CO_NR])(void) = {
	[M0_CO_DIR]        = m0_conf__dir_create,
	[M0_CO_PROFILE]    = m0_conf__profile_create,
	[M0_CO_FILESYSTEM] = m0_conf__filesystem_create,
	[M0_CO_SERVICE]    = m0_conf__service_create,
	[M0_CO_NODE]       = m0_conf__node_create,
	[M0_CO_NIC]        = m0_conf__nic_create,
	[M0_CO_SDEV]       = m0_conf__sdev_create,
	[M0_CO_PARTITION]  = m0_conf__partition_create
};

M0_INTERNAL struct m0_conf_obj *m0_conf_obj_create(struct m0_conf_cache *cache,
						   enum m0_conf_objtype type,
						   const struct m0_fid *id)
{
	struct m0_conf_obj *obj;

	M0_PRE(cache != NULL && m0_fid_is_set(id));
	M0_PRE(IS_IN_ARRAY(type, concrete_ctors));

	/* Allocate concrete object; initialise concrete fields. */
	obj = concrete_ctors[type]();
	if (obj == NULL)
		return NULL;

	/* Initialise generic fields. */
	obj->co_id = *id;
	obj->co_type = type;
	obj->co_status = M0_CS_MISSING;
	obj->co_cache = cache;

	m0_chan_init(&obj->co_chan, cache->ca_lock);

	m0_conf_cache_tlink_init(obj);
	m0_conf_dir_tlink_init(obj);
	m0_conf_obj_bob_init(obj);
	M0_ASSERT(obj->co_gen_magic == M0_CONF_OBJ_MAGIC);
	M0_ASSERT(M0_IN(obj->co_con_magic, (M0_CONF_DIR_MAGIC,
					    M0_CONF_PROFILE_MAGIC,
					    M0_CONF_FILESYSTEM_MAGIC,
					    M0_CONF_SERVICE_MAGIC,
					    M0_CONF_NODE_MAGIC,
					    M0_CONF_NIC_MAGIC,
					    M0_CONF_SDEV_MAGIC,
					    M0_CONF_PARTITION_MAGIC)));
	M0_ASSERT(!obj->co_mounted);

	M0_POST(m0_conf_obj_invariant(obj));
	return obj;
}

static int stub_create(struct m0_conf_cache *cache, enum m0_conf_objtype type,
		       const struct m0_fid *id, struct m0_conf_obj **out)
{
	int rc;

	M0_ENTRY();

	*out = m0_conf_obj_create(cache, type, id);
	if (*out == NULL)
		M0_RETURN(-ENOMEM);

	rc = m0_conf_cache_add(cache, *out);
	if (rc != 0) {
		m0_conf_obj_delete(*out);
		*out = NULL;
	}
	M0_RETURN(rc);
}

M0_INTERNAL int m0_conf_obj_find(struct m0_conf_cache *cache,
				 enum m0_conf_objtype type,
				 const struct m0_fid *id,
				 struct m0_conf_obj **out)
{
	int rc = 0;

	M0_ENTRY();
	M0_PRE(m0_mutex_is_locked(cache->ca_lock));

	*out = m0_conf_cache_lookup(cache, type, id);
	if (*out == NULL)
		rc = stub_create(cache, type, id, out);

	M0_POST(ergo(rc == 0,
		     m0_conf_obj_invariant(*out) && (*out)->co_cache == cache));
	M0_RETURN(rc);
}

M0_INTERNAL void m0_conf_obj_delete(struct m0_conf_obj *obj)
{
	M0_PRE(m0_conf_obj_invariant(obj));
	M0_PRE(obj->co_nrefs == 0);
	M0_PRE(obj->co_status != M0_CS_LOADING);
	M0_PRE(!obj->co_mounted || m0_mutex_is_locked(obj->co_cache->ca_lock));

	/* Finalise generic fields. */
	m0_conf_obj_bob_fini(obj);
	m0_conf_dir_tlink_fini(obj);
	m0_conf_cache_tlink_fini(obj);
	m0_chan_fini(&obj->co_chan);

	/* Finalise concrete fields; free the object. */
	obj->co_ops->coo_delete(obj);
}

M0_INTERNAL void m0_conf_obj_get(struct m0_conf_obj *obj)
{
	M0_PRE(m0_conf_obj_invariant(obj));
	M0_PRE(m0_mutex_is_locked(obj->co_cache->ca_lock));
	M0_PRE(obj->co_status == M0_CS_READY);

	M0_CNT_INC(obj->co_nrefs);
}

M0_INTERNAL void m0_conf_obj_put(struct m0_conf_obj *obj)
{
	M0_PRE(m0_conf_obj_invariant(obj));
	M0_PRE(m0_mutex_is_locked(obj->co_cache->ca_lock));
	M0_PRE(obj->co_status == M0_CS_READY);

	M0_CNT_DEC(obj->co_nrefs);
	if (obj->co_nrefs == 0)
		m0_chan_broadcast(&obj->co_chan);
}

/** Performs sanity checking of given m0_confx_obj. */
static bool confx_obj_is_valid(const struct m0_confx_obj *flat)
{
	/* XXX TODO
	 * - All of m0_fid-s contained in `flat' are m0_fid_is_set();
	 * - all of arr_fid-s are populated with valid m0_fid-s;
	 * - etc.
	 */
	(void)flat; /* XXX */
	return true;
}

M0_INTERNAL int m0_conf_obj_fill(struct m0_conf_obj *dest,
				 const struct m0_confx_obj *src,
				 struct m0_conf_cache *cache)
{
	int rc;

	M0_ENTRY();
	M0_PRE(m0_conf_obj_invariant(dest));
	M0_PRE(m0_mutex_is_locked(cache->ca_lock));
	M0_PRE(m0_conf_obj_is_stub(dest) && dest->co_nrefs == 0);
	M0_PRE(dest->co_type == src->o_conf.u_type);
	M0_PRE(m0_fid_eq(&dest->co_id, &src->o_id));
	M0_PRE(confx_obj_is_valid(src));

	rc = dest->co_ops->coo_decode(dest, src, cache);
	dest->co_status = rc == 0 ? M0_CS_READY : M0_CS_MISSING;

	M0_POST(ergo(rc == 0, dest->co_mounted));
	M0_POST(m0_mutex_is_locked(cache->ca_lock));
	M0_POST(m0_conf_obj_invariant(dest));
	M0_LEAVE("retval=%d", rc);
	return rc;
}

M0_INTERNAL bool m0_conf_obj_match(const struct m0_conf_obj *cached,
				   const struct m0_confx_obj *flat)
{
	M0_PRE(m0_conf_obj_invariant(cached));
	M0_PRE(confx_obj_is_valid(flat));

	return cached->co_type == flat->o_conf.u_type &&
		m0_fid_eq(&cached->co_id, &flat->o_id) &&
		(m0_conf_obj_is_stub(cached) ||
		 cached->co_ops->coo_match(cached, flat));
}

/** @} conf_dlspec_objops */
#undef M0_TRACE_SUBSYSTEM
