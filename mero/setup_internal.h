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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 28-Jun-2012
 */

#pragma once

#ifndef __MERO_SETUP_INTERNAL_H__
#define __MERO_SETUP_INTERNAL_H__

#include "mero/setup.h"

/**
   @addtogroup m0d
   @{
 */

/** Represents list of buffer pools in the mero context. */
struct cs_buffer_pool {
	/** Network buffer pool object. */
	struct m0_net_buffer_pool cs_buffer_pool;
	/** Linkage into network buffer pool list. */
	struct m0_tlink           cs_bp_linkage;
	uint64_t                  cs_bp_magic;
};

/**
 * Obtains configuration data from confd and converts it into options,
 * understood by _args_parse().
 *
 * @param[out] args   Arguments to be filled.
 * @param confd_addr  Endpoint address of confd service.
 * @param profile     The name of configuration profile.
 * @param local_addr  Endpoint address of local node.
 */
M0_INTERNAL int cs_conf_to_args(struct cs_args *args, const char *confd_addr,
				const char *profile, const char *local_addr);

/**
 * Fills CLI arguments based on contents of genders file for localhost.
 */
M0_INTERNAL int cs_genders_to_args(struct cs_args *args, const char *argv0,
				   const char *genders);

/** @} endgroup m0d */

#endif /* __MERO_SETUP_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
