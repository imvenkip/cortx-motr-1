/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 08-Mar-2015
 */


/**
 * @addtogroup addb2
 *
 * @{
 */

#include "lib/varr.h"
#include "lib/mutex.h"

#include "addb2/identifier.h"

static struct m0_varr  value_id;
static struct m0_mutex value_id_lock;

void m0_addb2_value_id_set(struct m0_addb2_value_descr *descr)
{
	const struct m0_addb2_value_descr **addr;

	m0_mutex_lock(&value_id_lock);
	if (descr->vd_id < m0_varr_size(&value_id)) {
		addr = m0_varr_ele_get(&value_id, descr->vd_id);
		if (addr != NULL)
			*addr = descr;
	}
	m0_mutex_unlock(&value_id_lock);
}

void m0_addb2_value_id_set_nr(struct m0_addb2_value_descr *descr)
{
	while (descr->vd_id != 0)
		m0_addb2_value_id_set(descr++);
}

struct m0_addb2_value_descr *m0_addb2_value_id_get(uint64_t id)
{
	struct m0_addb2_value_descr **addr;
	struct m0_addb2_value_descr  *descr = NULL;

	m0_mutex_lock(&value_id_lock);
	if (id < m0_varr_size(&value_id)) {
		addr = m0_varr_ele_get(&value_id, id);
		if (addr != NULL)
			descr = *addr;
	}
	m0_mutex_unlock(&value_id_lock);
	return descr;
}

M0_INTERNAL int m0_addb2_identifier_module_init(void)
{
	int result;

	result = m0_varr_init(&value_id, M0_AVI_LAST, sizeof(char *), 4096);
	if (result == 0) {
		m0_mutex_init(&value_id_lock);
		m0_addb2_value_id_set
			(&(struct m0_addb2_value_descr){ M0_AVI_NULL, "null"});
	}
	return result;
}

M0_INTERNAL void m0_addb2_identifier_module_fini(void)
{
	m0_mutex_fini(&value_id_lock);
	m0_varr_fini(&value_id);
}

/** @} end of addb2 group */

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
