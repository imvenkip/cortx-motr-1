#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdarg.h>
#include <err.h>

#include <db.h>

#if DB_VERSION_MAJOR == 5
#  include <db_int.h>
#endif

#include <dbinc/db_swap.h>

#include "lib/errno.h"
#include "dbtypes.h"
#include "fol.h"

int fol_create_read(DB_ENV *dbenv, void *recbuf, fol_create_args **argpp);
int fol_create_print(DB_ENV *dbenv, DBT *dbtp, DB_LSN *lsnp, 
		     db_recops notused2);

/*
 * fol_create_recover --
 *	Recovery function for create.
 *
 * PUBLIC: int fol_create_recover
 * PUBLIC:   __P((dbenv *, DBT *, DB_LSN *, db_recops));
 */
static int db4_fol_create_recover(DB_ENV *dbenv, DBT *dbtp, 
			          DB_LSN *lsnp, db_recops op)
{
	fol_create_args *argp;
	int cmp_n, cmp_p, modified, ret;

	(void)fol_create_print(dbenv, dbtp, lsnp, op);

	argp = NULL;
	if ((ret = fol_create_read(dbenv, dbtp->data, &argp)) != 0)
		goto out;

	modified = 0;
	cmp_n = 0;
	cmp_p = 0;

	/*
	 * The function now needs to calculate cmp_n and cmp_p based
	 * on whatever is in argp (usually an LSN representing the state
	 * of an object BEFORE the operation described in this record was
	 * applied) and whatever other information the function needs,
	 * e.g., the LSN of the object as it exists now.
	 *
	 * cmp_p should be set to 0 if the current state of the object
	 * is believed to be same as the state of the object BEFORE the
	 * described operation was applied.  For example, if you had an
	 * LSN in the log record (argp->prevlsn) and a current LSN of the
	 * object (curlsn), you might want to do:
	 *
	 * cmp_p = log_compare(curlsn, argp->prevlsn);
	 *
	 * Similarly, cmp_n should be set to 0 if the current state
	 * of the object reflects the object AFTER this operation has
	 * been applied.  Thus, if you can figure out an object's current
	 * LSN, yo might set cmp_n as:
	 *
	 * cmp_n = log_compare(lsnp, curlsn);
	 */
	if (cmp_p == 0 && DB_REDO(op)) {
		/* Need to redo update described. */
		modified = 1;
	} else if (cmp_n == 0 && !DB_REDO(op)) {
		/* Need to undo update described. */
		modified = 1;
	}

	/* Allow for following LSN pointers through a transaction. */
	*lsnp = argp->prev_lsn;
	ret = 0;

out:	if (argp != NULL)
		free(argp);

	return (ret);
}

int fol_dispatch(DB_ENV *dbenv, DBT *dbt, DB_LSN *lsn, db_recops op)
{
	return db4_fol_create_recover(dbenv, dbt, lsn, op);
}

