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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/22/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sns/repair/ut/cp_common.h"

/* Populates the bufvec with a character value. */
void bv_populate(struct m0_bufvec *b, char data, uint32_t seg_nr,
		 uint32_t seg_size)
{
        int i;

        M0_UT_ASSERT(b != NULL);
        M0_UT_ASSERT(m0_bufvec_alloc(b, seg_nr, seg_size) == 0);
        M0_UT_ASSERT(b->ov_vec.v_nr == seg_nr);
        for (i = 0; i < seg_nr; ++i) {
                M0_UT_ASSERT(b->ov_vec.v_count[i] == seg_size);
                M0_UT_ASSERT(b->ov_buf[i] != NULL);
                memset(b->ov_buf[i], data, seg_size);
        }
}

/* Compares 2 bufvecs and asserts if not equal. */
void bv_compare(struct m0_bufvec *b1, struct m0_bufvec *b2, uint32_t seg_nr,
		uint32_t seg_size)
{
        int i;

        M0_UT_ASSERT(b1 != NULL);
        M0_UT_ASSERT(b2 != NULL);
        M0_UT_ASSERT(b1->ov_vec.v_nr == seg_nr);
        M0_UT_ASSERT(b2->ov_vec.v_nr == seg_nr);

        for (i = 0; i < seg_nr; ++i) {
                M0_UT_ASSERT(b1->ov_vec.v_count[i] == seg_size);
                M0_UT_ASSERT(b1->ov_buf[i] != NULL);
                M0_UT_ASSERT(b2->ov_vec.v_count[i] == seg_size);
                M0_UT_ASSERT(b2->ov_buf[i] != NULL);
                M0_UT_ASSERT(memcmp(b1->ov_buf[i], b2->ov_buf[i],
                                    seg_size) == 0);
        }
}

inline void bv_free(struct m0_bufvec *b)
{
        m0_bufvec_free(b);
}

void cp_prepare(struct m0_cm_cp *cp, struct m0_bufvec *bv,
		uint32_t bv_seg_nr, uint32_t bv_seg_size,
		struct m0_sns_repair_ag *sns_ag,
		char data, struct m0_fom_ops *cp_fom_ops)
{
        M0_UT_ASSERT(cp != NULL);
        M0_UT_ASSERT(bv != NULL);
        M0_UT_ASSERT(sns_ag != NULL);

        bv_populate(bv, data, bv_seg_nr, bv_seg_size);
        cp->c_ag = &sns_ag->sag_base;
        m0_cm_cp_init(cp);
        cp->c_data = bv;
        cp->c_fom.fo_ops = cp_fom_ops;
        cp->c_ops = &m0_sns_repair_cp_ops;
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
