/* Do not edit: automatically built by gen_rec.awk. */

#ifndef	fol_AUTO_H
#define	fol_AUTO_H
#define	DB_fol_create	20001
typedef struct _fol_create_args {
	u_int32_t type;
	DB_TXN *txnp;
	DB_LSN prev_lsn;
	u32	fc_epoch0;
	u32	fc_epoch1;
	u32	fc_lsn0;
	u32	fc_lsn1;
	u32	fc_pfid0;
	u32	fc_pfid1;
	u32	fc_pfid2;
	u32	fc_pfid3;
	u32	fc_pver0;
	u32	fc_pver1;
	u32	fc_flags;
	u32	fc_uid;
	u32	fc_gid;
	u32	fc_cfid0;
	u32	fc_cfid1;
	u32	fc_cfid2;
	u32	fc_cfid3;
	u32	fc_cver0;
	u32	fc_cver1;
	DBT	fc_name;
} fol_create_args;

#endif
