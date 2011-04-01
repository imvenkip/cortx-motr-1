#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
/* Use RPC #include "net/net.h" */
#include "addb/addb.h"

#ifdef __KERNEL__
# include "addb_k.h"
#else
# include "addb_u.h"
#endif

int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_stob *stob)
{
	const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
	struct c2_addb_record        rec;
	int rc;

	if (ops->aeo_pack == NULL)
		return 0;

	C2_SET0(&rec);
	/* get size */
	rec.ar_data.cmb_count = ops->aeo_getsize(dp);
	if (rec.ar_data.cmb_count != 0) {
		rec.ar_data.cmb_value = c2_alloc(rec.ar_data.cmb_count);
		if (rec.ar_data.cmb_value == NULL)
			return -ENOMEM;
	}
	/* packing */
	rc = ops->aeo_pack(dp, &rec);
	if (rc == 0) {
		/* use stob io routines to write the addb */
#ifndef __KERNEL__
		/*
		   XXX Write to file just for an example in DLD phase.
		   Writing into stob file should be implemented in code phase.
		 */
		FILE * log_file = (FILE*)stob;

		/* apend this record into the stob */
		fwrite(&rec.ar_header, sizeof (struct c2_addb_record_header),
			1, log_file);
		fwrite(rec.ar_data.cmb_value, rec.ar_data.cmb_count,
			1, log_file);
#endif
	}
	c2_free(rec.ar_data.cmb_value);
	return rc;
}
C2_EXPORTED(c2_addb_stob_add);

int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_table *table)
{
	/*
	 TODO store this addb record into db.
	 */
	return 0;
}
C2_EXPORTED(c2_addb_db_add);

/* Use RPC */
/* int c2_addb_net_add(struct c2_addb_dp *dp, struct c2_net_conn *conn) */
/* { */
/* 	const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops; */
/* 	struct c2_fop         *request; */
/* 	struct c2_fop         *reply; */
/* 	struct c2_addb_record *addb_record; */
/* 	struct c2_addb_reply  *addb_reply; */
/* 	struct c2_net_call    call; */
/* 	int size; */
/* 	int result; */

/* 	if (ops->aeo_pack == NULL) */
/* 		return 0; */

/* 	request = c2_fop_alloc(&c2_addb_record_fopt, NULL); */
/* 	reply   = c2_fop_alloc(&c2_addb_reply_fopt, NULL); */
/* 	if (request == NULL || reply == NULL) { */
/* 		result = -ENOMEM; */
/* 		goto out; */
/* 	} */

/* 	addb_record = c2_fop_data(request); */
/* 	addb_reply  = c2_fop_data(reply); */

/* 	/\* get size *\/ */
/* 	size = ops->aeo_getsize(dp); */
/* 	if (size != 0) { */
/* 		addb_record->ar_data.cmb_value = c2_alloc(size); */
/* 		if (addb_record->ar_data.cmb_value == NULL) { */
/* 			result = -ENOMEM; */
/* 			goto out; */
/* 		} */
/* 	} else */
/* 		addb_record->ar_data.cmb_value = NULL; */
/* 	addb_record->ar_data.cmb_count = size; */
/* 	/\* packing *\/ */
/* 	result = ops->aeo_pack(dp, addb_record); */
/* 	if (result == 0) { */
/* 		C2_SET0(addb_reply); */
/* 		call.ac_arg = request; */
/* 		call.ac_ret = reply; */
/* 		result = c2_net_cli_call(conn, &call); */
/* 		/\* call c2_rpc_item_submit() in the future *\/ */
/* 	} */
/* 	c2_free(addb_record->ar_data.cmb_value); */
/* out: */
/* 	c2_fop_free(request); */
/* 	c2_fop_free(reply); */

/* 	return result; */
/* } */
/* C2_EXPORTED(c2_addb_net_add); */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
