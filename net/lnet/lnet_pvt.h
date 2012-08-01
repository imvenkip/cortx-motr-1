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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 11/16/2011
 */

#pragma once

#ifndef __COLIBRI_NET_LNET_PVT_H__
#define __COLIBRI_NET_LNET_PVT_H__

/**
   @addtogroup LNetXODFS
   @{
 */

#define LNET_ADDB_FUNCFAIL_ADD(ctx, rc)					\
	C2_ADDB_ADD(&(ctx), &nlx_addb_loc, nlx_func_fail, __func__, (rc))

#define NLX_ADDB_ADD(ctx, ev, ...)					\
	C2_ADDB_ADD(&(ctx), &nlx_addb_loc, ev , ## __VA_ARGS__)

#define LNET_ADDB_STAT_ADD(ctx, ...)					\
({									\
	struct nlx_addb_dp __dp = {					\
		.ad_dp.ad_ctx   = &(ctx),				\
		.ad_dp.ad_loc   = &nlx_addb_loc,			\
		.ad_dp.ad_ev    = &nlx_qstat,				\
		.ad_dp.ad_level = c2_addb_level_default,		\
	};								\
									\
	(void) sizeof(((__nlx_qstat_typecheck_t *)NULL)			\
		      (&__dp.ad_dp , ## __VA_ARGS__));			\
	if (nlx_qstat.ae_ops->aeo_subst(&__dp.ad_dp , ## __VA_ARGS__) == 0) \
		c2_addb_add(&__dp.ad_dp);				\
})

/* forward references to other static functions */
static bool nlx_tm_invariant(const struct c2_net_transfer_mc *tm);
static void nlx_tm_ev_worker(struct c2_net_transfer_mc *tm);
static bool nlx_ep_invariant(const struct c2_net_end_point *ep);
static int nlx_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_transfer_mc *tm,
			 const struct nlx_core_ep_addr *cepa);
static bool nlx_xo_buffer_bufvec_invariant(const struct c2_net_buffer *nb);

/**
   Inline helper to get the Core EP address pointer from an end point.
 */
static inline
struct nlx_core_ep_addr *nlx_ep_to_core(struct c2_net_end_point *ep)
{
	struct nlx_xo_ep *xep;
	C2_PRE(nlx_ep_invariant(ep));
	xep = container_of(ep, struct nlx_xo_ep, xe_ep);
	return &xep->xe_core;
}

static int nlx_xo_core_bev_to_net_bev(struct c2_net_transfer_mc *tm,
				      struct nlx_core_buffer_event *lcbev,
				      struct c2_net_buffer_event *nbev);

static c2_time_t nlx_tm_get_buffer_timeout_tick(const struct
						c2_net_transfer_mc *tm);
static int nlx_tm_timeout_buffers(struct c2_net_transfer_mc *tm, c2_time_t now);

/** @} */ /* LNetXODFS */

/**
   @addtogroup LNetCore
   @{
 */

/* forward references to other static functions */
static bool nlx_core_buffer_invariant(const struct nlx_core_buffer *cb);
static bool nlx_core_tm_is_locked(const struct nlx_core_transfer_mc *ctm);

/**
   Compute the checksum for a memory location reference.
 */
static uint32_t nlx_core_kmem_loc_checksum(const struct nlx_core_kmem_loc *loc);

/**
   Test if a memory location reference is valid.
 */
static inline bool nlx_core_kmem_loc_invariant(
					   const struct nlx_core_kmem_loc *loc)
{
	return loc != NULL && loc->kl_page != NULL &&
		loc->kl_checksum == nlx_core_kmem_loc_checksum(loc);
}

/**
   Test if a memory location object is empty (not set).
 */
static inline bool nlx_core_kmem_loc_is_empty(
					   const struct nlx_core_kmem_loc *loc)
{
	return loc->kl_page == NULL &&
		loc->kl_offset == 0 &&
		loc->kl_checksum == 0;
}

/**
   Compare memory location objects for equality.
   These objects cannot be compared atomically.  The caller must ensure
   that appropriate synchronization is used if the objects can change.
 */
static inline bool nlx_core_kmem_loc_eq(const struct nlx_core_kmem_loc *a,
					const struct nlx_core_kmem_loc *b)
{
	if (a == NULL || b == NULL)
		return a == b;
	return a->kl_page == b->kl_page &&
		a->kl_offset == b->kl_offset &&
		a->kl_checksum == b->kl_checksum;
}

/**
   Decodes a NID string into a NID.
   @param lcdom The domain private data.
   @param nidstr the string to be decoded.
   @param nid On success, the resulting NID is returned here.
   @retval -EINVAL the NID string could not be decoded
 */
static int nlx_core_nidstr_decode(struct nlx_core_domain *lcdom,
				  const char *nidstr,
				  uint64_t *nid);

/**
   Encode a NID into its string representation.
   @param lcdom The domain private data.
   @param nid The NID to be converted.
   @param nidstr On success, the string form is set here.
 */
static int nlx_core_nidstr_encode(struct nlx_core_domain *lcdom,
				  uint64_t nid,
				  char nidstr[C2_NET_LNET_NIDSTR_SIZE]);

/** @} */ /* LNetCore */

#endif /* __COLIBRI_NET_LNET_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
