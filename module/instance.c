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

#include "module/instance.h"

/**
 * @addtogroup module
 *
 * @{
 */

M0_INTERNAL struct m0 *m0_get(void)
{
	struct m0 *result = m0_thread_tls()->tls_m0_instance;
	M0_POST(result != NULL);
	return result;
}

M0_INTERNAL void m0_set(struct m0 *instance)
{
	M0_PRE(instance != NULL);
	m0_thread_tls()->tls_m0_instance = instance;
}

static const struct m0_modlev levels_inst[] = {
	[M0_LEVEL_INIT] = {
		.ml_name  = "m0 is initialised"
	}
};

M0_INTERNAL void m0_instance_setup(struct m0 *instance)
{
	m0_module_setup(&instance->i_self, "m0 instance",
			levels_inst, ARRAY_SIZE(levels_inst));
	instance->i_self.m_m0 = instance;
	m0_net_module_setup(&instance->i_net);
	m0_module_dep_add(&instance->i_self, M0_LEVEL_INIT,
			  &instance->i_net.n_module, M0_LEVEL_NET);
}

/** @} module */
