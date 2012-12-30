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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
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
	M0_ENTRY();

	m0_confx_fini(enc->ec_objs, enc->ec_nr);
	m0_free(enc->ec_objs);

	M0_LEAVE();
}

M0_INTERNAL int
m0_conf_parse(const char *src, struct confx_object *dest, size_t n)
{
	struct enconf enc;
	int i;
	int rc;

	M0_ENTRY();

	M0_SET0(&enc);
	rc = m0_xcode_read(&M0_XCODE_OBJ(enconf_xc, &enc), src);
	if (rc != 0) {
		M0_ASSERT(rc < 0);
		M0_LOG(M0_NOTICE, "conf string: %s", src);
		M0_RETURN(rc);
	}

	if (enc.ec_nr > n) {
		enconf_fini(&enc);
		M0_RETURN(-ENOMEM);
	}

	for (i = 0; i < enc.ec_nr; ++i)
		dest[i] = enc.ec_objs[i];

	/* Note that we don't call enconf_fini(): the caller should
	 * decide when to free confx objects. */
	m0_free(enc.ec_objs);

	M0_LEAVE();
	return enc.ec_nr;
}

M0_INTERNAL size_t m0_confx_obj_nr(const char *src)
{
	struct enconf enc;
	int rc;

	M0_ENTRY();

	M0_SET0(&enc);
	rc = m0_xcode_read(&M0_XCODE_OBJ(enconf_xc, &enc), src);
	if (rc != 0) {
		M0_ASSERT(rc < 0);
		M0_RETURN(rc);
	}

	enconf_fini(&enc);
	M0_LEAVE();
	return enc.ec_nr;
}

/* XXX Code duplication! See arrbuf_free() in conf/objs/common.[hc]. */
static void arr_buf_free(struct arr_buf *arr)
{
	M0_ENTRY();

	while (arr->ab_count > 0)
		m0_buf_free(&arr->ab_elems[--arr->ab_count]);
	m0_free(arr->ab_elems);
	arr->ab_elems = NULL;

	M0_POST(arr->ab_count == 0);
	M0_LEAVE();
}

M0_INTERNAL void m0_confx_fini(struct confx_object *xobjs, size_t n)
{
	struct confx_u *x;
	int             i;

	M0_ENTRY();

	for (i = 0; i < n; ++i) {
		m0_buf_free(&xobjs[i].o_id);
		x = &xobjs[i].o_conf;

		switch(x->u_type) {
		case M0_CO_PROFILE:
			m0_buf_free(&x->u.u_profile.xp_filesystem);
			break;
		case M0_CO_FILESYSTEM:
			arr_buf_free(&x->u.u_filesystem.xf_params);
			arr_buf_free(&x->u.u_filesystem.xf_services);
			break;
		case M0_CO_SERVICE:
			arr_buf_free(&x->u.u_service.xs_endpoints);
			m0_buf_free(&x->u.u_service.xs_node);
			break;
		case M0_CO_NODE:
			arr_buf_free(&x->u.u_node.xn_nics);
			arr_buf_free(&x->u.u_node.xn_sdevs);
			break;
		case M0_CO_NIC:
			m0_buf_free(&x->u.u_nic.xi_filename);
			break;
		case M0_CO_SDEV:
			arr_buf_free(&x->u.u_sdev.xd_partitions);
			m0_buf_free(&x->u.u_sdev.xd_filename);
			break;
		case M0_CO_PARTITION:
			m0_buf_free(&x->u.u_partition.xa_file);
			break;
		default:
			M0_IMPOSSIBLE("Unexpected value of confx_u::u_type");
		}
	}
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM
