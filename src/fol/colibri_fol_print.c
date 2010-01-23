/* Do not edit: automatically built by gen_rec.awk. */

#include "db_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "db.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <err.h>
#include <db.h>
#include <dbinc/db_swap.h>
#include <db_int.h>
#include "dbtypes.h"
#include "colibri_fol.h"
/*
 * PUBLIC: int colibri_fol_create_print __P((DB_ENV *, DBT *,
 * PUBLIC:     DB_LSN *, db_recops));
 */
int
colibri_fol_create_print(dbenv, dbtp, lsnp, notused2)
	DB_ENV *dbenv;
	DBT *dbtp;
	DB_LSN *lsnp;
	db_recops notused2;
{
	colibri_fol_create_args *argp;
	int colibri_fol_create_read __P((DB_ENV *, void *, colibri_fol_create_args **));
	u_int32_t i;
	int ch;
	int ret;

	notused2 = DB_TXN_PRINT;

	if ((ret = colibri_fol_create_read(dbenv, dbtp->data, &argp)) != 0)
		return (ret);
	(void)printf(
    "[%lu][%lu]colibri_fol_create%s: rec: %lu txnp %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file, (u_long)lsnp->offset,
	    (argp->type & DB_debug_FLAG) ? "_debug" : "",
	    (u_long)argp->type,
	    (u_long)argp->txnp->txnid,
	    (u_long)argp->prev_lsn.file, (u_long)argp->prev_lsn.offset);
	(void)printf("\tfc_epoch0: 0x%lx\n", (u_long)argp->fc_epoch0);
	(void)printf("\tfc_epoch1: 0x%lx\n", (u_long)argp->fc_epoch1);
	(void)printf("\tfc_lsn0: 0x%lx\n", (u_long)argp->fc_lsn0);
	(void)printf("\tfc_lsn1: 0x%lx\n", (u_long)argp->fc_lsn1);
	(void)printf("\tfc_pfid0: 0x%lx\n", (u_long)argp->fc_pfid0);
	(void)printf("\tfc_pfid1: 0x%lx\n", (u_long)argp->fc_pfid1);
	(void)printf("\tfc_pfid2: 0x%lx\n", (u_long)argp->fc_pfid2);
	(void)printf("\tfc_pfid3: 0x%lx\n", (u_long)argp->fc_pfid3);
	(void)printf("\tfc_pver0: 0x%lx\n", (u_long)argp->fc_pver0);
	(void)printf("\tfc_pver1: 0x%lx\n", (u_long)argp->fc_pver1);
	(void)printf("\tfc_flags: 0x%lx\n", (u_long)argp->fc_flags);
	(void)printf("\tfc_uid: 0x%lx\n", (u_long)argp->fc_uid);
	(void)printf("\tfc_gid: 0x%lx\n", (u_long)argp->fc_gid);
	(void)printf("\tfc_cfid0: 0x%lx\n", (u_long)argp->fc_cfid0);
	(void)printf("\tfc_cfid1: 0x%lx\n", (u_long)argp->fc_cfid1);
	(void)printf("\tfc_cfid2: 0x%lx\n", (u_long)argp->fc_cfid2);
	(void)printf("\tfc_cfid3: 0x%lx\n", (u_long)argp->fc_cfid3);
	(void)printf("\tfc_cver0: 0x%lx\n", (u_long)argp->fc_cver0);
	(void)printf("\tfc_cver1: 0x%lx\n", (u_long)argp->fc_cver1);
	(void)printf("\tfc_name: ");
	for (i = 0; i < argp->fc_name.size; i++) {
		ch = ((u_int8_t *)argp->fc_name.data)[i];
		printf(isprint(ch) || ch == 0x0a ? "%c" : "%#x ", ch);
	}
	(void)printf("\n");
	(void)printf("\n");
	free(argp);
	return (0);
}

/*
 * PUBLIC: int colibri_fol_init_print __P((DB_ENV *, DB_DISTAB *));
 */
int
colibri_fol_init_print(dbenv, dtabp)
	DB_ENV *dbenv;
	DB_DISTAB *dtabp;
{
	int __db_add_recovery __P((DB_ENV *, DB_DISTAB *,
	    int (*)(DB_ENV *, DBT *, DB_LSN *, db_recops), u_int32_t));
	int ret;

	if ((ret = __db_add_recovery(dbenv, dtabp,
	    colibri_fol_create_print, DB_colibri_fol_create)) != 0)
		return (ret);
	return (0);
}
