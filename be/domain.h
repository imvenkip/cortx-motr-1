/* -*- C -*- */
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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 18-Jul-2013
 */

#pragma once

#ifndef __MERO_BE_DOMAIN_H__
#define __MERO_BE_DOMAIN_H__

#include "be/engine.h"		/* m0_be_engine */
#include "be/seg0.h"		/* m0_be_0type */
#include "lib/tlist.h"		/* m0_tl */

/**
 * @defgroup be Meta-data back-end
 *
 * @{
 */

struct m0_be_tx;
struct m0_be_0type;
struct m0_be_log;

struct m0_be_0type_log_cfg {
	uint64_t    blc_stob_key;
	m0_bcount_t blc_size;
};

struct m0_be_0type_seg_cfg {
	uint64_t     bsc_stob_key;
	bool	     bsc_preallocate;
	m0_bcount_t  bsc_size;
	void	    *bsc_addr;
};

struct m0_be_domain_cfg {
	/** BE engine configuration. */
	struct m0_be_engine_cfg	     bc_engine;
	/**
	 * Stob domain location.
	 * Stobs for log and segments are in this domain.
	 */
	const char		    *bc_stob_domain_location;
	/**
	 * str_cfg_init parameter for m0_stob_domain_init() (in normal mode)
	 * and m0_stob_domain_create() (in mkfs mode).
	 */
	const char		    *bc_stob_domain_cfg_init;
	/**
	 * seg0 stob key.
	 * This field is ignored in mkfs mode.
	 */
	uint64_t		     bc_seg0_stob_key;
	/**
	 * mkfs mode flag.
	 * It makes difference in m0_be_domain_start() only. After this function
	 * called BE domain behaviur is the same for mkfs and normal modes.
	 */
	bool			     bc_mkfs_mode;

	/*
	 * Next fields are for mkfs mode only.
	 * They are completely ignored in normal mode.
	 */

	/** str_cfg_create parameter for m0_stob_domain_create(). */
	const char		    *bc_stob_domain_cfg_create;
	/**
	 * Stob domain key for BE stobs. Stob domain with this key is
	 * created at m0_be_domain_cfg::bc_stob_domain_location.
	 */
	uint64_t		     bc_stob_domain_key;
	/** BE log configuration. */
	struct m0_be_0type_log_cfg   bc_log_cfg;
	/** BE seg0 configuration. */
	struct m0_be_0type_seg_cfg   bc_seg0_cfg;
	/**
	 * Array of segment configurations.
	 * - can be NULL;
	 * - should be NULL if m0_be_domain_cfg::bc_seg_nr == 0.
	 * */
	struct m0_be_0type_seg_cfg  *bc_seg_cfg;
	/** Size of m0_be_domain_cfg::bc_seg_cfg array. */
	unsigned		     bc_seg_nr;
	/**
	 * mkfs progress callback. Can be NULL.
	 * @param stage_index Current stage index. It's value is in range
	 *		      [0, stage_nr).
	 * @param stage_nr    Total number of stages. It is constant across
	 *		      It is constant for each callback call.
	 * @param msg	      Text message with stage description. Can be NULL.
	 *
	 * This callback is called exactly stage_nr times in case of mkfs
	 * success and <= stage_nr times in case of failure.
	 */
	void			   (*bc_mkfs_progress_cb)
				     (unsigned	  stage_index,
				      unsigned	  stage_nr,
				      const char *msg);
};

struct m0_be_domain {
	struct m0_be_domain_cfg	bd_cfg;
	struct m0_be_engine	bd_engine;
	struct m0_mutex		bd_lock;
	struct m0_tl            bd_0type_list;
	/** List of segments in this domain. First segment in which is seg0. */
	struct m0_tl            bd_seg_list;
	struct m0_be_seg	bd_seg0;
	struct m0_stob         *bd_seg0_stob;
	struct m0_stob_domain  *bd_stob_domain;
	struct m0_be_0type	bd_0type_log;
	struct m0_be_0type	bd_0type_seg;
	unsigned		bd_mkfs_stage;
	unsigned		bd_mkfs_stage_nr;
	/** XXX please remove it when db5 emulation removed */
	void		       *bd_db_impl;
};

M0_INTERNAL int m0_be_domain_init(struct m0_be_domain *dom);
/**
 * This function in normal (not mkfs) mode:
 * - loads seg0 (domain should be started in mkfs mode before it can be started
 *   in normal mode);
 * - processes all 0types stored in seg0. Log and all segments in domain are
 *   initialized, along with other 0types.
 *
 * This function in mkfs mode:
 * - creates BE seg0 and log on storage (configuration is taken from dom_cfg);
 * - creates segs_nr segments (configuration for each segment is taken from
 *   segs_cfg);
 * - executes progress callback to notify user about stages executed.
 *
 * In either case after successful function call BE domain is ready to work.
 *
 * @see m0_be_domain_cfg, m0_be_domain_seg_create().
 */
M0_INTERNAL int m0_be_domain_start(struct m0_be_domain	   *dom,
				   struct m0_be_domain_cfg *cfg);

M0_INTERNAL void m0_be_domain_fini(struct m0_be_domain *dom);

M0_INTERNAL struct m0_be_tx *m0_be_domain_tx_find(struct m0_be_domain *dom,
						  uint64_t id);

M0_INTERNAL struct m0_be_engine *m0_be_domain_engine(struct m0_be_domain *dom);
M0_INTERNAL
struct m0_be_seg *m0_be_domain_seg0_get(struct m0_be_domain *dom);
M0_INTERNAL struct m0_be_log *m0_be_domain_log(struct m0_be_domain *dom);
M0_INTERNAL bool m0_be_domain_is_locked(const struct m0_be_domain *dom);

/**
 * Returns existing BE segment if @addr is inside it. Returns NULL otherwise.
 */
M0_INTERNAL struct m0_be_seg *m0_be_domain_seg(const struct m0_be_domain *dom,
					       const void		 *addr);
/**
 * Returns existing be-segment by its @id. If no segments found return NULL.
 */
M0_INTERNAL struct m0_be_seg *
m0_be_domain_seg_by_id(const struct m0_be_domain *dom, uint64_t id);

M0_INTERNAL void
m0_be_domain_seg_create_credit(struct m0_be_domain		*dom,
			       const struct m0_be_0type_seg_cfg *seg_cfg,
			       struct m0_be_tx_credit		*cred);
M0_INTERNAL void m0_be_domain_seg_destroy_credit(struct m0_be_domain	*dom,
						 struct m0_be_seg	*seg,
						 struct m0_be_tx_credit *cred);

M0_INTERNAL int
m0_be_domain_seg_create(struct m0_be_domain		  *dom,
			struct m0_be_tx			  *tx,
			const struct m0_be_0type_seg_cfg  *seg_cfg,
			struct m0_be_seg		 **out);
/**
 * Destroy functions for the segment dictionary and for the segment allocator
 * are not called.
 *
 * Current code doesn't do all necessary cleaning to be sure that nothing is
 * allocated and there is no entry in segment dictionary before segment is
 * destroyed.
 *
 * However, it may be a problem with seg0 entries if objects they are describing
 * were on destroyed segment. It may even lead to ABA problem. There is no such
 * mechanism (yet) to keep track of these "lost" entries, so they are
 * responsibility of a developer.
 */
M0_INTERNAL int m0_be_domain_seg_destroy(struct m0_be_domain *dom,
					 struct m0_be_tx     *tx,
					 struct m0_be_seg    *seg);

/* for internal be-usage only */
M0_INTERNAL void m0_be_domain__0type_register(struct m0_be_domain *dom,
					      struct m0_be_0type *type);
M0_INTERNAL void m0_be_domain__0type_unregister(struct m0_be_domain *dom,
						struct m0_be_0type *type);

/** @} end of be group */
#endif /* __MERO_BE_DOMAIN_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
