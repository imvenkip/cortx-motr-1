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
        m0_addb_ctx_type_register(&m0_addb_ct_sns_mod);
        m0_addb_ctx_type_register(&m0_addb_ct_sns_cm);
        m0_addb_ctx_type_register(&m0_addb_ct_sns_ag);
        m0_addb_ctx_type_register(&m0_addb_ct_sns_cp);

        m0_addb_rec_type_register(&m0_addb_rt_sns_cm_buf_nr);
        m0_addb_rec_type_register(&m0_addb_rt_sns_ag_alloc);
        m0_addb_rec_type_register(&m0_addb_rt_sns_ag_fini);
        m0_addb_rec_type_register(&m0_addb_rt_sns_sw_update);
        m0_addb_rec_type_register(&m0_addb_rt_sns_iter_next_gfid);
        m0_addb_rec_type_register(&m0_addb_rt_sns_ag_info);
        m0_addb_rec_type_register(&m0_addb_rt_sns_cp_info);
	m0_addb_rec_type_register(&m0_addb_rt_sns_repair_info);
	m0_addb_rec_type_register(&m0_addb_rt_sns_repair_progress);

	m0_xc_sns_cp_onwire_init();
	m0_xc_sw_onwire_fop_init();
	m0_xc_trigger_fop_init();
	return m0_sns_cm_type_register();
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
