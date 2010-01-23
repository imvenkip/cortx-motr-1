/* Do not edit: automatically built by gen_rec.awk. */

#include "db_config.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "db_int.h"
#include "dbinc/db_swap.h"
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
 * PUBLIC: int colibri_fol_create_read __P((DB_ENV *, void *,
 * PUBLIC:     colibri_fol_create_args **));
 */
int
colibri_fol_create_read(dbenv, recbuf, argpp)
	DB_ENV *dbenv;
	void *recbuf;
	colibri_fol_create_args **argpp;
{
	colibri_fol_create_args *argp;
	u_int32_t uinttmp;
	u_int8_t *bp;
	ENV *env;

	env = dbenv->env;

	if ((argp = malloc(sizeof(colibri_fol_create_args) + sizeof(DB_TXN))) == NULL)
		return (ENOMEM);
	bp = recbuf;
	argp->txnp = (DB_TXN *)&argp[1];
	memset(argp->txnp, 0, sizeof(DB_TXN));

	LOGCOPY_32(env, &argp->type, bp);
	bp += sizeof(argp->type);

	LOGCOPY_32(env, &argp->txnp->txnid, bp);
	bp += sizeof(argp->txnp->txnid);

	LOGCOPY_TOLSN(env, &argp->prev_lsn, bp);
	bp += sizeof(DB_LSN);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_epoch0 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_epoch1 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_lsn0 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_lsn1 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_pfid0 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_pfid1 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_pfid2 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_pfid3 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_pver0 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_pver1 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_flags = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_uid = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_gid = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_cfid0 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_cfid1 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_cfid2 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_cfid3 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_cver0 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	LOGCOPY_32(env, &uinttmp, bp);
	argp->fc_cver1 = (u32)uinttmp;
	bp += sizeof(uinttmp);

	memset(&argp->fc_name, 0, sizeof(argp->fc_name));
	LOGCOPY_32(env,&argp->fc_name.size, bp);
	bp += sizeof(u_int32_t);
	argp->fc_name.data = bp;
	bp += argp->fc_name.size;

	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int colibri_fol_create_log __P((DB_ENV *, DB_TXN *,
 * PUBLIC:     DB_LSN *, u_int32_t, u32, u32, u32, u32, u32, u32, u32, u32,
 * PUBLIC:     u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32,
 * PUBLIC:     const DBT *));
 */
int
colibri_fol_create_log(dbenv, txnp, ret_lsnp, flags,
    fc_epoch0, fc_epoch1, fc_lsn0, fc_lsn1, fc_pfid0, fc_pfid1,
    fc_pfid2, fc_pfid3, fc_pver0, fc_pver1, fc_flags, fc_uid,
    fc_gid, fc_cfid0, fc_cfid1, fc_cfid2, fc_cfid3, fc_cver0,
    fc_cver1, fc_name)
	DB_ENV *dbenv;
	DB_TXN *txnp;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u32 fc_epoch0;
	u32 fc_epoch1;
	u32 fc_lsn0;
	u32 fc_lsn1;
	u32 fc_pfid0;
	u32 fc_pfid1;
	u32 fc_pfid2;
	u32 fc_pfid3;
	u32 fc_pver0;
	u32 fc_pver1;
	u32 fc_flags;
	u32 fc_uid;
	u32 fc_gid;
	u32 fc_cfid0;
	u32 fc_cfid1;
	u32 fc_cfid2;
	u32 fc_cfid3;
	u32 fc_cver0;
	u32 fc_cver1;
	const DBT *fc_name;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn, *rlsnp;
	ENV *env;
	u_int32_t zero, uinttmp, rectype, txn_num;
	u_int npad;
	u_int8_t *bp;
	int ret;

	env = dbenv->env;
	rlsnp = ret_lsnp;
	rectype = DB_colibri_fol_create;
	npad = 0;
	ret = 0;

	if (txnp == NULL) {
		txn_num = 0;
		lsnp = &null_lsn;
		null_lsn.file = null_lsn.offset = 0;
	} else {
		/*
		 * We need to assign begin_lsn while holding region mutex.
		 * That assignment is done inside the DbEnv->log_put call,
		 * so pass in the appropriate memory location to be filled
		 * in by the log_put code.
		 */
		DB_SET_TXN_LSNP(txnp, &rlsnp, &lsnp);
		txn_num = txnp->txnid;
	}

	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t)
	    + sizeof(u_int32_t) + (fc_name == NULL ? 0 : fc_name->size);
	if ((logrec.data = malloc(logrec.size)) == NULL)
		return (ENOMEM);
	bp = logrec.data;

	if (npad > 0)
		memset((u_int8_t *)logrec.data + logrec.size - npad, 0, npad);

	bp = logrec.data;

	LOGCOPY_32(env, bp, &rectype);
	bp += sizeof(rectype);

	LOGCOPY_32(env, bp, &txn_num);
	bp += sizeof(txn_num);

	LOGCOPY_FROMLSN(env, bp, lsnp);
	bp += sizeof(DB_LSN);

	uinttmp = (u_int32_t)fc_epoch0;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_epoch1;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_lsn0;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_lsn1;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_pfid0;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_pfid1;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_pfid2;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_pfid3;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_pver0;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_pver1;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_flags;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_uid;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_gid;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_cfid0;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_cfid1;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_cfid2;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_cfid3;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_cver0;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	uinttmp = (u_int32_t)fc_cver1;
	LOGCOPY_32(env,bp, &uinttmp);
	bp += sizeof(uinttmp);

	if (fc_name == NULL) {
		zero = 0;
		LOGCOPY_32(env, bp, &zero);
		bp += sizeof(u_int32_t);
	} else {
		LOGCOPY_32(env, bp, &fc_name->size);
		bp += sizeof(fc_name->size);
		memcpy(bp, fc_name->data, fc_name->size);
		bp += fc_name->size;
	}

	if ((ret = dbenv->log_put(dbenv, rlsnp, (DBT *)&logrec,
	    flags | DB_LOG_NOCOPY)) == 0 && txnp != NULL) {
		*lsnp = *rlsnp;
		if (rlsnp != ret_lsnp)
			 *ret_lsnp = *rlsnp;
	}
#ifdef LOG_DIAGNOSTIC
	if (ret != 0)
		(void)colibri_fol_create_print(dbenv,
		    (DBT *)&logrec, ret_lsnp, DB_TXN_PRINT);
#endif

	free(logrec.data);
	return (ret);
}

