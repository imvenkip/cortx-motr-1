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
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 03/17/2011
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "addb/addb.h"

/*
   XXX
   all interfaces here are only for building the pseudu
   addb library, which is used to build fop/fop2c, which is used
   to build full addb library
*/

struct c2_addb_record {

};

int c2_addb_func_fail_getsize(struct c2_addb_dp *dp) { return 8; }

int c2_addb_func_fail_pack(struct c2_addb_dp *dp,
			   struct c2_addb_record *rec) { return 0; }

int c2_addb_call_getsize(struct c2_addb_dp *dp) { return 0; }
int c2_addb_call_pack(struct c2_addb_dp *dp,
		      struct c2_addb_record *rec) { return 0; }

int c2_addb_flag_getsize(struct c2_addb_dp *dp) { return 0; }
int c2_addb_flag_pack(struct c2_addb_dp *dp,
		      struct c2_addb_record *rec) { return 0; }

int c2_addb_inval_getsize(struct c2_addb_dp *dp) { return 0; }
int c2_addb_inval_pack(struct c2_addb_dp *dp,
		       struct c2_addb_record *rec) { return 0; }

int c2_addb_empty_getsize(struct c2_addb_dp *dp) { return 0; }
int c2_addb_empty_pack(struct c2_addb_dp *dp,
		       struct c2_addb_record *rec) { return 0; }

int c2_addb_trace_getsize(struct c2_addb_dp *dp) { return 8; }

int c2_addb_trace_pack(struct c2_addb_dp *dp,
		       struct c2_addb_record *rec) { return 0; }

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
