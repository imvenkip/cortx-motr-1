#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "udb.h"

/**
   @addtogroup udb
   @{
 */


int c2_udb_ctxt_init(struct c2_udb_ctxt *ctxt)
{
	/* TODO add more here. Now it is a stub */
	return 0;
}

void c2_udb_ctxt_fini(struct c2_udb_ctxt *ctxt)
{

	/* TODO add more here. Now it is a stub */
	return;
}

int c2_udb_add(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_domain *edomain,
	       const struct c2_udb_cred *external,
	       const struct c2_udb_cred *internal)
{
	/* TODO add more here. Now it is a stub */
	return 0;
}

int c2_udb_del(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_domain *edomain,
	       const struct c2_udb_cred *external,
	       const struct c2_udb_cred *internal)
{

	/* TODO add more here. Now it is a stub */
	return 0;
}

int c2_udb_e2i(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_cred *external,
	       struct c2_udb_cred *internal)
{

	/* TODO add more here. Now it is a stub */
	return 0;
}

int c2_udb_i2e(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_cred *internal,
	       struct c2_udb_cred *external)
{

	/* TODO add more here. Now it is a stub */
	return 0;
}

/** @} end group udb */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
