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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09-Sep-2010
 */

#include "lib/errno.h"         /* EINVAL */
#include "lib/misc.h"          /* memcmp, strcmp */
#include "lib/string.h"        /* sscanf */
#include "lib/assert.h"        /* M0_PRE */
#include "lib/hash.h"          /* m0_hash */
#include "lib/arith.h"         /* m0_rnd */
#include "fid/fid_xc.h"
#include "fid/fid.h"

/**
   @addtogroup fid

   @{
 */

/* TODO move to m0 */
static const struct m0_fid_type *fid_types[256];

M0_INTERNAL void m0_fid_type_register(const struct m0_fid_type *fidt)
{
	uint8_t id = fidt->ft_id;

	M0_PRE(IS_IN_ARRAY(id, fid_types));
	M0_PRE(fid_types[id] == NULL);
	fid_types[id] = fidt;
}

M0_INTERNAL void m0_fid_type_unregister(const struct m0_fid_type *fidt)
{
	uint8_t id = fidt->ft_id;

	M0_PRE(IS_IN_ARRAY(id, fid_types));
	M0_PRE(fid_types[id] == fidt);
	fid_types[id] = NULL;
}

M0_INTERNAL const struct m0_fid_type *m0_fid_type_get(uint8_t id)
{
	M0_PRE(IS_IN_ARRAY(id, fid_types));
	return fid_types[id];
}

M0_INTERNAL const struct m0_fid_type *m0_fid_type_gethi(uint64_t id)
{
	return m0_fid_type_get(id >> (64 - 8));
}

M0_INTERNAL const struct m0_fid_type *
m0_fid_type_getfid(const struct m0_fid *fid)
{
	return m0_fid_type_gethi(fid->f_container);
}

M0_INTERNAL const struct m0_fid_type *m0_fid_type_getname(const char *name)
{
	size_t i;
	const struct m0_fid_type *fidt;

	for (i = 0; i < ARRAY_SIZE(fid_types); ++i) {
		fidt = fid_types[i];
		M0_ASSERT(ergo(fidt != NULL, fidt->ft_name != NULL));
		if (fidt != NULL && strcmp(name, fidt->ft_name) == 0)
			return fidt;
	}

	return NULL;
}

M0_INTERNAL bool m0_fid_is_valid(const struct m0_fid *fid)
{
	const struct m0_fid_type *ft = m0_fid_type_getfid(fid);

	return
		ft != NULL &&
		ergo(ft->ft_is_valid != NULL, ft->ft_is_valid(fid));
}
M0_EXPORTED(m0_fid_is_valid);

M0_INTERNAL bool m0_fid_is_set(const struct m0_fid *fid)
{
	static const struct m0_fid zero = {
		.f_container = 0,
		.f_key = 0
	};
	return !m0_fid_eq(fid, &zero);
}
M0_EXPORTED(m0_fid_is_set);

M0_INTERNAL void m0_fid_set(struct m0_fid *fid, uint64_t container,
			    uint64_t key)
{
	M0_PRE(fid != NULL);

	fid->f_container = container;
	fid->f_key = key;
}
M0_EXPORTED(m0_fid_set);

M0_INTERNAL void m0_fid_tset(struct m0_fid *fid,
			     uint8_t tid, uint64_t container, uint64_t key)
{
	m0_fid_set(fid, M0_FID_TCONTAINER(tid, container), key);
}
M0_EXPORTED(m0_fid_tset);

M0_INTERNAL uint8_t m0_fid_tget(const struct m0_fid *fid)
{
	return fid->f_container >> 56;
}
M0_EXPORTED(m0_fid_tget);

M0_INTERNAL void m0_fid_tchange(struct m0_fid *fid, uint8_t tid)
{
	M0_PRE(fid != NULL);
	M0_PRE(m0_fid_is_set(fid));

	fid->f_container = M0_FID_TCONTAINER(tid, fid->f_container);

	M0_POST(m0_fid_is_valid(fid));
}

M0_INTERNAL void m0_fid_tassume(struct m0_fid *fid,
				const struct m0_fid_type *ft)
{
	M0_PRE(fid != NULL);
	M0_PRE(ft != NULL);

	m0_fid_tchange(fid, ft->ft_id);
}

M0_INTERNAL bool m0_fid_eq(const struct m0_fid *fid0, const struct m0_fid *fid1)
{
	return memcmp(fid0, fid1, sizeof *fid0) == 0;
}
M0_EXPORTED(m0_fid_eq);

M0_INTERNAL int m0_fid_cmp(const struct m0_fid *fid0, const struct m0_fid *fid1)
{
	const struct m0_uint128 u0 = {
		.u_hi = fid0->f_container,
		.u_lo = fid0->f_key
	};

	const struct m0_uint128 u1 = {
		.u_hi = fid1->f_container,
		.u_lo = fid1->f_key
	};

	return m0_uint128_cmp(&u0, &u1);
}
M0_EXPORTED(m0_fid_cmp);

M0_INTERNAL int m0_fid_sscanf(const char *s, struct m0_fid *fid)
{
	int rc = sscanf(s, FID_SF, FID_S(fid));
	return rc == 2 ? 0 : -EINVAL;
}

M0_INTERNAL int m0_fid_sscanf_simple(const char *s, struct m0_fid *fid)
{
	int rc = sscanf(s, " %"SCNx64" : %"SCNx64" ", FID_S(fid));
	return rc == 2 ? 0 : -EINVAL;
}

/**
 * Type of miscellaneous fids used in tests, etc.
 */
static const struct m0_fid_type misc = {
	.ft_id   = 0,
	.ft_name = "miscellaneous"
};

M0_INTERNAL int m0_fid_init(void)
{
	m0_fid_type_register(&misc);
	m0_xc_fid_init();
	return 0;
}
M0_EXPORTED(m0_fid_init);

M0_INTERNAL void m0_fid_fini(void)
{
	m0_fid_type_unregister(&misc);
	m0_xc_fid_fini();
}
M0_EXPORTED(m0_fid_fini);

M0_INTERNAL uint64_t m0_fid_hash(const struct m0_fid *fid, uint64_t max,
				 uint32_t i)

{
	uint64_t hash;
	uint64_t index;

	hash = m0_hash(fid->f_container + fid->f_key + i);
	index = m0_rnd(max, &hash);
	M0_ASSERT(index < max);
	return index;
}

/** @} end of fid group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
