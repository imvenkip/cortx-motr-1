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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/errno.h"
#include "sns/parity_repair.h"

static void device_index_get(const struct m0_fid *fid,
			     struct m0_pdclust_layout *pl,
			     struct m0_pdclust_instance *pi,
			     uint64_t group_number, uint64_t unit_number,
			     uint32_t *device_index_out)
{
        struct m0_fid               cob_fid;
        struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
        struct m0_layout_enum      *le;

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
}

M0_INTERNAL int m0_sns_repair_spare_map(struct m0_poolmach *pm,
					const struct m0_fid *fid,
					struct m0_pdclust_layout *pl,
					struct m0_pdclust_instance *pi,
					uint64_t group_number,
					uint64_t unit_number,
					uint32_t *spare_slot_out,
					uint32_t *spare_slot_out_prev)
{
        uint32_t device_index;
        uint32_t device_index_new;
        int      rc;

	M0_PRE(pm != NULL && fid != NULL && pl != NULL);

        device_index_get(fid, pl, pi, group_number, unit_number, &device_index);
	*spare_slot_out_prev = unit_number;

        while (1) {
                rc = m0_poolmach_sns_repair_spare_query(pm, device_index,
                                                        spare_slot_out);
                if (rc != 0)
                        return rc;

		/*
		 * Find out if spare slot's corresponding device index is
		 * failed. If yes, find out new spare.
		 */
		device_index_get(fid, pl, pi, group_number,
				 m0_pdclust_N(pl) + m0_pdclust_K(pl) +
				 *spare_slot_out, &device_index_new);

                if (m0_poolmach_device_is_in_spare_usage_array(pm,
							device_index_new)) {
                        device_index = device_index_new;
			*spare_slot_out_prev = *spare_slot_out;
		} else
                        break;
        }
	/*
	 * Return the absolute index of spare with respect to the aggregation
	 * group.
	 */
        if (rc == 0) {
                *spare_slot_out += m0_pdclust_N(pl) + m0_pdclust_K(pl);
		*spare_slot_out_prev += m0_pdclust_N(pl) + m0_pdclust_K(pl);
	}

        return rc;
}

static bool frame_eq(struct m0_pdclust_instance *pi, uint64_t group_number,
                     uint64_t frame, uint32_t device_index)
{
        struct m0_pdclust_src_addr sa;
        struct m0_pdclust_tgt_addr ta;

        M0_PRE(pi != NULL);

        M0_SET0(&sa);
        M0_SET0(&ta);

        ta.ta_frame = frame;
        ta.ta_obj = device_index - 1;

        m0_pdclust_instance_inv(pi, &ta, &sa);
        return sa.sa_group == group_number;
}

static uint64_t frame_get(struct m0_pdclust_instance *pi, uint64_t spare_frame,
			  uint64_t group_number, uint32_t device_index)
{
        uint64_t                   frame;
        bool                       frame_found;

        M0_PRE(pi != NULL);

        /* Start with the (group_number - 1), to match the frame. */
        if (spare_frame != 0) {
                frame = spare_frame - 1;
                frame_found = frame_eq(pi, group_number, frame, device_index);
                if (frame_found)
                        goto out;
        }

        frame = spare_frame;
        frame_found = frame_eq(pi, group_number, frame, device_index);
        if (frame_found)
                goto out;

        frame = spare_frame + 1;
        frame_found = frame_eq(pi, group_number, frame, device_index);

out:
        if (frame_found)
                return frame;
        else
                return -ENOENT;
}

M0_INTERNAL int m0_sns_repair_data_map(struct m0_poolmach *pm,
                                       const struct m0_fid *fid,
                                       struct m0_pdclust_layout *pl,
                                       uint64_t group_number,
                                       uint64_t spare_unit_number,
                                       uint64_t *data_unit_id_out)
{
        int                         rc;
        struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
        struct m0_layout_instance  *li;
        struct m0_pdclust_instance *pi;
	enum m0_pool_nd_state       state_out;
	uint64_t                    spare_in;
        uint32_t                    device_index;
        uint64_t                    spare_id;
        uint64_t                    frame;

        M0_PRE(pm != NULL && fid != NULL && pl != NULL);

	rc = m0_layout_instance_build(&pl->pl_base.sl_base, fid, &li);
	if (rc != 0)
		return -ENOENT;

	pi = m0_layout_instance_to_pdi(li);
	spare_in = spare_unit_number;

	do {
		spare_id = spare_in - m0_pdclust_N(pl) -
			m0_pdclust_K(pl);
		/*
		 * Fetch the correspinding data/parity unit device index for
		 * the given spare unit.
		 */
		device_index = pm->pm_state->pst_spare_usage_array[spare_id].
			psu_device_index;

		if (device_index == POOL_PM_SPARE_SLOT_UNUSED) {
			rc = -ENOENT;
			goto out;
		}

		M0_SET0(&sa);
		M0_SET0(&ta);
		sa.sa_group = group_number;
		sa.sa_unit  = spare_in;
		m0_pdclust_instance_map(pi, &sa, &ta);
		/*
		 * Find the data/parity unit frame for the @group_number on the
		 * given device represented by @device_index.
		 */
		frame = frame_get(pi, ta.ta_frame, group_number, device_index);
		if (frame == -ENOENT) {
			rc = -ENOENT;
			goto out;
		}

		M0_SET0(&sa);
		M0_SET0(&ta);

		ta.ta_frame = frame;
		ta.ta_obj = device_index - 1;

		rc = m0_poolmach_device_state(pm, device_index, &state_out);
		if (rc != 0) {
			rc = -ENOENT;
			goto out;
		}

		/*
		 * Doing inverse mapping from the frame in the device to the
		 * corresponding unit in parity group @group_number.
		 */
		m0_pdclust_instance_inv(pi, &ta, &sa);

		*data_unit_id_out = sa.sa_unit;

		/*
		 * It is possible that the unit mapped corresponding to the given
		 * spare_unit_number is same as the spare_unit_number.
		 * Thus this means that there is no data/parity unit repaired on
		 * the given spare_unit_number and the spare is empty.
		 */
		if (spare_unit_number == sa.sa_unit) {
			rc = -ENOENT;
			goto out;
		}

		/*
		 * We have got another spare unit, so further try again to map
		 * this spare unit to the actual failed data/parity unit.
		 */
		spare_in = sa.sa_unit;

	} while(m0_pdclust_unit_classify(pl, sa.sa_unit) == M0_PUT_SPARE &&
			M0_IN(state_out, (M0_PNDS_SNS_REPAIRED, M0_PNDS_SNS_REBALANCING)));
out:
	m0_layout_instance_fini(&pi->pi_base);

	return rc;
}

#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
