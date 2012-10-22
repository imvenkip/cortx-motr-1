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
void bv_populate(struct c2_bufvec *b, char data)
{
        int i;

        C2_UT_ASSERT(b != NULL);
        C2_UT_ASSERT(c2_bufvec_alloc(b, SEG_NR, SEG_SIZE) == 0);
        C2_UT_ASSERT(b->ov_vec.v_nr == SEG_NR);
        for (i = 0; i < SEG_NR; ++i) {
                C2_UT_ASSERT(b->ov_vec.v_count[i] == SEG_SIZE);
                C2_UT_ASSERT(b->ov_buf[i] != NULL);
                memset(b->ov_buf[i], data, SEG_SIZE);
        }
}

/* Compares 2 bufvecs and asserts if not equal. */
void bv_compare(struct c2_bufvec *b1, struct c2_bufvec *b2)
{
        int i;

        C2_UT_ASSERT(b1 != NULL);
        C2_UT_ASSERT(b2 != NULL);
        C2_UT_ASSERT(b1->ov_vec.v_nr == SEG_NR);
        C2_UT_ASSERT(b2->ov_vec.v_nr == SEG_NR);

        for (i = 0; i < SEG_NR; ++i) {
                C2_UT_ASSERT(b1->ov_vec.v_count[i] == SEG_SIZE);
                C2_UT_ASSERT(b1->ov_buf[i] != NULL);
                C2_UT_ASSERT(b2->ov_vec.v_count[i] == SEG_SIZE);
                C2_UT_ASSERT(b2->ov_buf[i] != NULL);
                C2_UT_ASSERT(memcmp(b1->ov_buf[i], b2->ov_buf[i],
                                    SEG_SIZE) == 0);
        }
}

inline void bv_free(struct c2_bufvec *b)
{
        c2_bufvec_free(b);
}

void cp_prepare(struct c2_cm_cp *cp, struct c2_bufvec *bv,
		struct c2_sns_repair_ag *sns_ag,
		char data, struct c2_fom_ops *cp_fom_ops)
{
        C2_UT_ASSERT(cp != NULL);
        C2_UT_ASSERT(bv != NULL);
        C2_UT_ASSERT(sns_ag != NULL);

        bv_populate(bv, data);
        cp->c_ag = &sns_ag->sag_base;
        c2_cm_cp_init(cp);
        cp->c_data = bv;
        cp->c_fom.fo_ops = cp_fom_ops;
        cp->c_ops = &c2_sns_repair_cp_ops;
        /* Required to pass the fom invariant */
        cp->c_fom.fo_fop = (void *)1;
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
