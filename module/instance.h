/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 18-Jan-2014
 */
#pragma once
#ifndef __MERO_MODULE_INSTANCE_H__
#define __MERO_MODULE_INSTANCE_H__

#include "module/module.h"  /* m0_module */
#include "net/module.h"     /* m0_net */
#include "stob/module.h"    /* m0_stob_module */
#include "ut/stob.h"	    /* m0_ut_stob_module */

struct m0_be_domain;
struct m0_dbenv;

/**
 * @addtogroup module
 *
 * @{
 */

/**
 * m0 instance.
 *
 * All "global" variables are accessible from this struct.
 *
 * Each module belongs to exactly one instance.
 *
 * m0 instance is allocated and initialised by external user.
 * There are three use cases:
 *
 * - test creating one or more m0 instances (only one currently, but
 *   multiple instances could be very convenient for testing);
 *
 * - mero/setup creating a single m0 instance for m0d;
 *
 * - m0t1fs creating a single m0 instance for kernel.
 */
struct m0 {
	/**
	 * Generation counter, modified each time a module or
	 * dependency is added. Used to detect when initialisation
	 * should re-start.
	 */
	uint64_t		  i_dep_gen;
	/** Module representing this instance. */
	struct m0_module	  i_self;

	/*
	 * Global modules.
	 *
	 * CAUTION: Do not add members willy-nilly!
	 *          Without proper care m0 instance will become another
	 *          Windows Registry.
	 *          Please negotiate new entries with vvv or Nikita.
	 */
	struct m0_net             i_net;
	struct m0_stob_module     i_stob_module;
	struct m0_stob_ad_module  i_stob_ad_module;
	struct m0_ut_stob_module  i_ut_stob_module;
	struct m0_be_domain      *i_be_dom;
	struct m0_be_domain      *i_be_dom_save;
	struct m0_be_ut_backend  *i_be_ut_backend;
	struct m0_be_ut_backend  *i_be_ut_backend_save;
	struct m0_dbenv          *i_dbenv;
	struct m0_dbenv          *i_dbenv_save;
	struct m0_poolmach_state *i_pool_module;
	struct m0_cob_domain     *i_cob_module;
	bool                      i_reqh_has_multiple_ad_domains;
	bool                      i_reqh_uses_ad_stob;
#if 0 /* XXX ENABLEME */
	/**
	 * Contains modules for library (thread, xc, etc.) together
	 * with their global data.
	 */
	struct m0_lib             i_lib;
	/* ... */
#endif
};

/** Configures m0_modules: m0 and its submodules. */
M0_INTERNAL void m0_instance_setup(struct m0 *instance);

/**
 * Returns current m0 instance.
 *
 * In the kernel, there is only one instance. It is returned.
 *
 * In user space, the instance is created by m0d early startup code
 * and stored in thread-local storage. This instance is inherited by
 * threads (i.e., when a thread is created it gets the instance of the
 * creator and stores it in its TLS).
 *
 * Theoretically, user space Mero can support multiple instances in
 * the same address space.
 *
 * @post retval != NULL
 */
M0_INTERNAL struct m0 *m0_get(void);

/**
 * Stores given pointer in thread-local storage.
 *
 * @pre instance != NULL
 */
M0_INTERNAL void m0_set(struct m0 *instance);

/**
 * Levels of m0 instance.
 *
 * Dependencies:
 * @verbatim
 *
 *   m0                     m0_net
 * +===============+      +--------------+
 * | M0_LEVEL_INIT |----->| M0_LEVEL_NET |
 * +---------------+      +--------------+
 *
 * @endverbatim
 */
enum {
	/** m0 instance and its submodules have been initialised. */
	M0_LEVEL_INIT
};

/** @} module */
#endif /* __MERO_MODULE_INSTANCE_H__ */
