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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 2/01/2012
 */

#include "addb/addb_xc.h"
#include "addb/addb.h"
#include "lib/arith.h" /* m0_align */

/* forward references within this file */
static int subst_name_uint64_t_qstats(struct m0_addb_dp *dp, const char *name,
				      uint64_t qid, struct m0_net_qstats *qs);
static int nlx_statistic_getsize(struct m0_addb_dp *dp);
static int nlx_statistic_pack(struct m0_addb_dp *dp,
			      struct m0_addb_record *rec);

/**
   @addtogroup LNetDFS
   @{
 */

static const struct m0_addb_loc nlx_addb_loc = {
	.al_name = "net-lnet"
};

/**
   Extended form of struct m0_addb_dp that includes the additional
   struct m0_net_qstats formal parameter.
 */
struct nlx_addb_dp {
	struct m0_addb_dp     ad_dp;
	struct m0_net_qstats *ad_qs;
};

/** A function failure event in net/lnet */
static M0_ADDB_EV_DEFINE(nlx_func_fail, "nlx_func_fail",
			 M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);

/** A named run-time queue statistic. */
static const struct m0_addb_ev_ops nlx_addb_qstats = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_name_uint64_t_qstats,
	.aeo_pack    = nlx_statistic_pack,
	.aeo_getsize = nlx_statistic_getsize,
	.aeo_size    = sizeof(uint64_t) + sizeof(struct m0_net_qstats) +
		       sizeof(char *),
	.aeo_name    = "qstats",
	.aeo_level   = AEL_INFO
};
typedef int __nlx_addb_qstats_typecheck_t(struct m0_addb_dp *dp,
					  const char *sname, uint64_t qid,
					  struct m0_net_qstats *qs);

/** A queue statistics event for net/lnet */
static M0_ADDB_EV_DEFINE(nlx_qstat, "nlx_qstat",
			 M0_ADDB_EVENT_NET_QSTATS, nlx_addb_qstats);

/**
   ADDB record body for run-time statistics event.
   This event includes a name, queue ID and qstats.
 */
struct nlx_qstat_body {
	uint64_t             sb_qid;
	struct m0_net_qstats sb_qstats;
	char                 sb_name[0];
};

M0_INTERNAL int nlx_statistic_getsize(struct m0_addb_dp *dp)
{
	return m0_align(sizeof(uint64_t) + sizeof(struct m0_net_qstats) +
			strlen(dp->ad_name) + 1, M0_ADDB_RECORD_LEN_ALIGN);
}

/** packing statistic addb record */
M0_INTERNAL int nlx_statistic_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec)
{
	struct m0_addb_record_header *header = &rec->ar_header;
	struct nlx_addb_dp           *ndp;
	struct nlx_qstat_body        *body;
	int rc;

	M0_ASSERT(dp != NULL);
	M0_ASSERT(nlx_statistic_getsize(dp) == rec->ar_data.cmb_count);

	ndp = container_of(dp, struct nlx_addb_dp, ad_dp);
	rc = m0_addb_record_header_pack(dp, header, rec->ar_data.cmb_count);
	if (rc == 0) {
		body = (struct nlx_qstat_body *)rec->ar_data.cmb_value;

		M0_ASSERT(body != NULL);
		body->sb_qid = dp->ad_rc;
		body->sb_qstats = *ndp->ad_qs;
		strcpy(body->sb_name, dp->ad_name);
	}
	return rc;
}

M0_INTERNAL int subst_name_uint64_t_qstats(struct m0_addb_dp *dp,
					   const char *name, uint64_t qid,
					   struct m0_net_qstats *qs)
{
	struct nlx_addb_dp *ndp;

	M0_ASSERT(dp != NULL && name != NULL && qs != NULL);
	ndp = container_of(dp, struct nlx_addb_dp, ad_dp);
	dp->ad_name = name;
	dp->ad_rc = qid;
	ndp->ad_qs = qs;
	return 0;
}

/**
   @}
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
