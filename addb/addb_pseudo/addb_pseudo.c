#ifdef HAVE_CONFIG_H
#  include <config.h>
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

int c2_addb_func_fail_getsize(struct c2_addb_dp *dp)
{
	return 8;
}

int c2_addb_func_fail_pack(struct c2_addb_dp *dp,
				  struct c2_addb_record *rec)
{
	return 0;
}

int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_stob *stob)
{
	return 0;
}
C2_EXPORTED(c2_addb_stob_add);

int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_table *table)
{
	return 0;
}
C2_EXPORTED(c2_addb_db_add);


int c2_addb_net_add(struct c2_addb_dp *dp, struct c2_net_conn *conn)
{
	return 0;
}
C2_EXPORTED(c2_addb_net_add);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
