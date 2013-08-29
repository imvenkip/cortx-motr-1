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

 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact

 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 03/30/2010
 */

#include "sns/sns_addb.h"
#include "sns/sns.h"
#include "sns/cm/trigger_fop_xc.h"
#include "sns/cm/sw_onwire_fop_xc.h"
#include "sns/cm/sns_cp_onwire_xc.h"
#include "cm/cp_onwire_xc.h"
#include "sns/cm/cm.h"

M0_INTERNAL int m0_sns_init()
{
	int rc;

#undef CT_REG
#define CT_REG(n) m0_addb_ctx_type_register(&m0_addb_ct_sns_##n)
        CT_REG(mod);
        CT_REG(cm);
        CT_REG(ag);
        CT_REG(cp);
#undef CT_REG
#undef RT_REG
#define RT_REG(n) m0_addb_rec_type_register(&m0_addb_rt_sns_##n)
        RT_REG(cm_buf_nr);
        RT_REG(ag_alloc);
        RT_REG(ag_fini);
        RT_REG(sw_update);
        RT_REG(iter_next_gfid);
        RT_REG(ag_info);
        RT_REG(cp_info);
	RT_REG(repair_info);
	RT_REG(repair_progress);
#undef RT_REG

	m0_xc_sns_cp_onwire_init();
	m0_xc_sw_onwire_fop_init();
	m0_xc_trigger_fop_init();
	rc = m0_sns_cm_type_register();

	return rc;
}

M0_INTERNAL void m0_sns_fini()
{
	m0_xc_sns_cp_onwire_fini();
	m0_xc_sw_onwire_fop_fini();
	m0_xc_trigger_fop_fini();
	m0_sns_cm_type_deregister();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
