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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 01/04/2011
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "capa.h"

/**
   @addtogroup capa
   @{
 */

int c2_capa_init(struct c2_capa_ctxt *ctxt)
{
	/* TODO This is only stub */
	return 0;
}

void c2_capa_fini(struct c2_capa_ctxt *ctxt)
{
	/* TODO This is only stub */
	return;
}

int c2_capa_new(struct c2_object_capa *capa,
	         enum c2_capa_entity_type type,
	         enum c2_capa_operation opcode,
		 void *data)
{
	capa->oc_ctxt = NULL;
	capa->oc_owner = NULL;
	capa->oc_type = type;
	capa->oc_opcode = opcode;
	capa->oc_data = data;
	c2_atomic64_set(&capa->oc_ref, 0);
	return 0;
}

int c2_capa_get(struct c2_capa_ctxt *ctxt, struct c2_capa_issuer *owner,
		struct c2_object_capa *capa)
{
	/* TODO This is only stub */
	capa->oc_ctxt = ctxt;
	capa->oc_owner = owner;

	c2_atomic64_inc(&capa->oc_ref);
	return 0;
}

void c2_capa_put(struct c2_capa_ctxt *ctxt, struct c2_object_capa *capa)
{
	/* TODO This is only stub */
	C2_ASSERT(c2_atomic64_get(&capa->oc_ref) > 0);
	c2_atomic64_dec(&capa->oc_ref);
	return;
}

int c2_capa_auth(struct c2_capa_ctxt *ctxt, struct c2_object_capa *capa,
		 enum c2_capa_operation op)
{
	/* TODO This is only stub */
	return 0;
}

int c2_capa_ctxt_init(struct c2_capa_ctxt *ctxt)
{
	/* TODO This is only stub */
	return 0;
}

void c2_capa_ctxt_fini(struct c2_capa_ctxt *ctxt)
{
	/* TODO This is only stub */
}

/** @} end group capa */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
