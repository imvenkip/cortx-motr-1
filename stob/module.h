/* -*- C -*- */
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
 * Original author: Dmytro Podgornyi <dmytro_podgornyi@xyratex.com>
 * Original creation date: 11-Mar-2014
 */

#pragma once
#ifndef __MERO_STOB_MODULE_H__
#define __MERO_STOB_MODULE_H__

#include "module/module.h"
#include "stob/type.h"

/**
 * @defgroup stob
 *
 * @{
 */

enum {
	M0_LEVEL_STOB = 1,
};

struct m0_stob_module {
	struct m0_module     stm_module;
	struct m0_stob_types stm_types;
};

M0_INTERNAL struct m0_stob_module *m0_stob_module__get(void);

extern struct m0_modlev m0_levels_stob[];
extern const unsigned m0_levels_stob_nr;

#define M0_STOB_INIT(instance) {                                         \
	.stm_module = M0_MODULE_INIT("stob module", (instance),          \
				     m0_levels_stob, m0_levels_stob_nr)  \
}

struct m0_stob_ad_module {
	struct m0_tl    sam_domains;
	struct m0_mutex sam_lock;
};

/** @} end of stob group */
#endif /* __MERO_STOB_MODULE_H__ */

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
