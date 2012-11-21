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

#include "conf/preload.h"
#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/obj.h"
#include "xcode/xcode.h"

#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"


#define XC_OBJ(xt, ptr) (&(struct c2_xcode_obj)	\
		{ .xo_type = (xt), .xo_ptr = (ptr) })

C2_INTERNAL int c2_conf_parse(const char *src, struct confx_object *dest,
			      size_t n)
{
	struct enconf rd_enc_ext;
	int           i;
	int	      result;

	C2_SET0(&rd_enc_ext);
	result = c2_xcode_read(XC_OBJ(enconf_xc, &rd_enc_ext), src);
	if (result != 0)
		return result;

	if (rd_enc_ext.ec_nr > n) {
		c2_confx_fini(rd_enc_ext.ec_objs, rd_enc_ext.ec_nr);
		c2_free(rd_enc_ext.ec_objs);
		return -ENOMEM;
	}

	for (i = 0; i < rd_enc_ext.ec_nr; ++i)
		dest[i] = rd_enc_ext.ec_objs[i];

	c2_free(rd_enc_ext.ec_objs);

	return rd_enc_ext.ec_nr;
}

C2_INTERNAL size_t c2_confx_obj_nr(const char *src)
{
	struct enconf rd_enc_ext;
	int	      result;

	C2_SET0(&rd_enc_ext);
	result = c2_xcode_read(XC_OBJ(enconf_xc, &rd_enc_ext), src);
	if (result != 0)
		return 0;

	c2_confx_fini(rd_enc_ext.ec_objs, rd_enc_ext.ec_nr);
	c2_free(rd_enc_ext.ec_objs);

	return rd_enc_ext.ec_nr;
}

static void arr_buf_fini(struct arr_buf *a)
{
	int i;

	for (i = 0; i < a->ab_count; ++i)
		c2_buf_free(&a->ab_elems[i]);

	c2_free(a->ab_elems);
}

C2_INTERNAL void c2_confx_fini(struct confx_object *xobjs, size_t n)
{
	int             i;
	struct confx_u *x;

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
}
