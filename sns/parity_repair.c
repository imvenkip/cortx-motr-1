/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 08/01/2013
 */

#include "lib/errno.h"
#include "sns/parity_repair.h"

static int device_index_get(const struct m0_fid *fid,
			    struct m0_pdclust_layout *pl,
			    uint64_t group_number, uint64_t unit_number,
			    uint32_t *device_index_out)
{
        struct m0_fid               cob_fid;
        struct m0_pdclust_instance *pi;
        struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
        struct m0_layout_enum      *le;
        struct m0_layout_instance  *li;
	int                         rc;

        rc = m0_layout_instance_build(&pl->pl_base.sl_base, fid, &li);
        if (rc != 0)
                return -ENOENT;

        pi = m0_layout_instance_to_pdi(li);
        le = m0_layout_to_enum(m0_pdl_to_layout(pl));

	/* Find out the device index. */
	M0_SET0(&sa);
	M0_SET0(&ta);
	M0_SET0(&cob_fid);

        sa.sa_group = group_number;
        sa.sa_unit = unit_number;
        m0_pdclust_instance_map(pi, &sa, &ta);
	m0_layout_enum_get(le, ta.ta_obj, fid, &cob_fid);
	*device_index_out = cob_fid.f_container;

	return 0;
}

M0_INTERNAL int m0_sns_repair_spare_map(struct m0_poolmach *pm,
					const struct m0_fid *fid,
					struct m0_pdclust_layout *pl,
					uint64_t group_number,
					uint64_t unit_number,
					uint32_t *spare_slot_out)
{
        uint32_t                    device_index;
        uint32_t                    device_index_new;
        int                         rc;

	M0_PRE(pm != NULL && fid != NULL && pl != NULL);
	/*
	 * This API should does not support finding out spare unit for a
	 * failed spare unit. Caller of this API should guarantee this
	 * precondition.
	 */
        M0_PRE(m0_pdclust_unit_classify(pl, unit_number) != M0_PUT_SPARE);

        rc = device_index_get(fid, pl, group_number, unit_number,
			      &device_index);
	if (rc != 0)
		return rc;

        while (1) {
                rc = m0_poolmach_sns_repair_spare_query(pm, device_index,
                                                        spare_slot_out);
                if (rc != 0)
                        return rc;

		/*
		 * Find out if spare slot's corresponding device index is
		 * failed. If yes, find out new spare.
		 */
		rc = device_index_get(fid, pl, group_number,
				      m0_pdclust_N(pl) + m0_pdclust_K(pl) +
				      *spare_slot_out,
				      &device_index_new);
		if (rc != 0)
			return rc;

                if (m0_poolmach_device_is_in_spare_usage_array(pm,
							device_index_new))
                        device_index = device_index_new;
                else
                        break;
        }
	/*
	 * Return the absolute index of spare with respect to the aggregation
	 * group.
	 */
        if (rc == 0)
                *spare_slot_out += m0_pdclust_N(pl) + m0_pdclust_K(pl);
        return rc;
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
