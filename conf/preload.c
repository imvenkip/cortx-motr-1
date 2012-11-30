/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 *                  Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 29-Aug-2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/preload.h"
#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/obj.h"
#include "xcode/xcode.h"

#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"

static void enconf_fini(struct enconf *enc)
{
	C2_ENTRY();

	c2_confx_fini(enc->ec_objs, enc->ec_nr);
	c2_free(enc->ec_objs);

	C2_LEAVE();
}

C2_INTERNAL int
c2_conf_parse(const char *src, struct confx_object *dest, size_t n)
{
	struct enconf enc;
	int i;
	int rc;

	C2_ENTRY();

	C2_SET0(&enc);
	rc = c2_xcode_read(&C2_XCODE_OBJ(enconf_xc, &enc), src);
	if (rc != 0) {
		C2_ASSERT(rc < 0);
		C2_RETURN(rc);
	}

	if (enc.ec_nr > n) {
		enconf_fini(&enc);
		C2_RETURN(-ENOMEM);
	}

	for (i = 0; i < enc.ec_nr; ++i)
		dest[i] = enc.ec_objs[i];

	/* Note that we don't call enconf_fini(): the caller should
	 * decide when to free confx objects. */
	c2_free(enc.ec_objs);

	C2_RETURN(enc.ec_nr);
}

C2_INTERNAL size_t c2_confx_obj_nr(const char *src)
{
	struct enconf enc;
	int rc;

	C2_ENTRY();

	C2_SET0(&enc);
	rc = c2_xcode_read(&C2_XCODE_OBJ(enconf_xc, &enc), src);
	if (rc != 0) {
		C2_ASSERT(rc < 0);
		C2_RETURN(rc);
	}

	enconf_fini(&enc);
	C2_RETURN(enc.ec_nr);
}

static void arr_buf_fini(struct arr_buf *a)
{
	int i;
	C2_ENTRY();

	for (i = 0; i < a->ab_count; ++i)
		c2_buf_free(&a->ab_elems[i]);
	c2_free(a->ab_elems);

	C2_LEAVE();
}

C2_INTERNAL void c2_confx_fini(struct confx_object *xobjs, size_t n)
{
	int             i;
	struct confx_u *x;

	C2_ENTRY();

	for (i = 0; i < n; ++i) {
		x = &xobjs[i].o_conf;

		switch(x->u_type) {
		case C2_CO_PROFILE:
			c2_buf_free(&x->u.u_profile.xp_filesystem);
			break;
		case C2_CO_FILESYSTEM:
			arr_buf_fini(&x->u.u_filesystem.xf_params);
			arr_buf_fini(&x->u.u_filesystem.xf_services);
			break;
		case C2_CO_SERVICE:
			arr_buf_fini(&x->u.u_service.xs_endpoints);
			c2_buf_free(&x->u.u_service.xs_node);
			break;
		case C2_CO_NODE:
			arr_buf_fini(&x->u.u_node.xn_nics);
			arr_buf_fini(&x->u.u_node.xn_sdevs);
			break;
		case C2_CO_NIC:
			c2_buf_free(&x->u.u_nic.xi_filename);
			break;
		case C2_CO_SDEV:
			arr_buf_fini(&x->u.u_sdev.xd_partitions);
			c2_buf_free(&x->u.u_sdev.xd_filename);
			break;
		case C2_CO_PARTITION:
			c2_buf_free(&x->u.u_partition.xa_file);
			break;
		default:
			C2_IMPOSSIBLE("Unexpected value of confx_u::u_type");
		}

		c2_buf_free(&xobjs[i].o_id);
	}
	C2_LEAVE();
}
