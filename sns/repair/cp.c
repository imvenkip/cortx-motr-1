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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/06/2012
 */

#include "lib/errno.h"
#include "lib/memory.h"

#include "sns/repair/cp.h"
#include "sns/repair/cm.h"

#if 0
static bool repair_cp_invaraint(struct c2_cm_cp *cp)
{
	return c2_cm_cp_invariant(cp);
}
#endif

static int repair_cp_alloc(struct c2_cm *cm, struct c2_cm_cp **cp)
{
	struct c2_sns_repair_cm *rcm;
	struct c2_sns_repair_cp	*rcp;
	struct c2_cm_cp		*lcp;
	struct c2_net_buffer	*nb;

	/* Allocate SNS repair copy packet.*/
	C2_ALLOC_PTR(rcp);
	if (rcp == NULL)
		return -ENOMEM;
	lcp = &rcp->rc_cp;

	/* Request data buffer from buffer pool.*/
	rcm = container_of(cm, struct c2_sns_repair_cm, rc_cm);
	c2_net_buffer_pool_lock(&rcm->rc_pool);
	nb = c2_net_buffer_pool_get(&rcm->rc_pool, C2_BUFFER_ANY_COLOUR);
	c2_net_buffer_pool_unlock(&rcm->rc_pool);
	C2_ASSERT(nb != NULL);
	lcp->c_data = &nb->nb_buffer;
	rcp->rc_phase = 0;

	/* Initailise copy packet data memebers.*/
	c2_cm_cp_init(cm, lcp, &c2_sns_repair_cp_ops);
	*cp = lcp;

	return 0;
}

static void repair_cp_free(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cm *rcm;
	struct c2_sns_repair_cp	*rcp;
	struct c2_net_buffer	*nb;

	/* Release data buffer to buffer pool.*/
	rcm = container_of(cp->c_cm, struct c2_sns_repair_cm, rc_cm);
	rcp = container_of(cp, struct c2_sns_repair_cp, rc_cp);
	nb = container_of(cp->c_data, struct c2_net_buffer, nb_buffer);
	c2_net_buffer_pool_lock(&rcm->rc_pool);
	c2_net_buffer_pool_put(&rcm->rc_pool, nb, C2_BUFFER_ANY_COLOUR);
	c2_net_buffer_pool_unlock(&rcm->rc_pool);

	/* finailise data members.*/
	c2_cm_cp_fini(cp);

	/* Free copy packet.*/
	c2_free(rcp);
}

static int repair_cp_read(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_write(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_send(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_recv(struct c2_cm_cp *cp)
{
        return 0;
}

int repair_cp_xform(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_state(struct c2_cm_cp *cp)
{
	return 0;
}

static void repair_cp_complete(struct c2_cm_cp *cp)
{
}

const struct c2_cm_cp_ops c2_sns_repair_cp_ops = {
	.co_alloc    = &repair_cp_alloc,
	.co_free     = &repair_cp_free,
	.co_read     = &repair_cp_read,
	.co_write    = &repair_cp_write,
	.co_send     = &repair_cp_send,
	.co_recv     = &repair_cp_recv,
	.co_xform    = &repair_cp_xform,
	.co_state    = &repair_cp_state,
	.co_complete = &repair_cp_complete
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

