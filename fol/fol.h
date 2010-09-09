/* -*- C -*- */

#ifndef __COLIBRI_FOL_FOL_H__
#define __COLIBRI_FOL_FOL_H__

/**
   @defgroup fol File operations log

   @{
 */

/* export */
struct c2_fol;
struct c2_fol_rec;

/* import */
#include "lib/types.h"    /* uint64_t */
#include "lib/mutex.h"
#include "fid/fid.h"
#include "dtm/dtm.h"      /* c2_update_id, c2_update_state */
#include "db/db.h"        /* c2_table, c2_db_cursor */

struct c2_dbenv;
struct c2_db_tx;
struct c2_epoch_id;

typedef uint64_t c2_fol_lsn_t;

struct c2_fol {
	struct c2_table f_table;
	c2_fol_lsn_t    f_lsn;
	struct c2_mutex f_lock;
};

int  c2_fol_init(struct c2_fol *fol, struct c2_dbenv *env);
void c2_fol_fini(struct c2_fol *fol);

int c2_fol_add(struct c2_fol *fol, struct c2_db_tx *tx, struct c2_fol_rec *rec);
int c2_fol_force(struct c2_fol *fol, uint64_t upto);

struct c2_fol_obj_ref {
	struct c2_fid for_fid;
	uint64_t      for_version;
	c2_fol_lsn_t  for_prevlsn;
};

struct c2_fol_update_ref {
	struct c2_update_id  fr_id;
	enum c2_update_state fr_state;
};

struct c2_fol_rec_ops;

struct c2_fol_rec {
	struct c2_fol               *fr_fol;
	c2_fol_lsn_t                 fr_lsn;
	uint32_t                     fr_opcode;

	uint32_t                     fr_obj_nr;
	struct c2_fol_obj_ref       *fr_ref;

	struct c2_epoch_id          *fr_epoch;
	struct c2_update_id         *fr_self;
	uint32_t                     fr_sibling_nr;
	struct c2_fol_update_ref    *fr_sibling;

	const struct c2_fol_rec_ops *fr_ops;
	void                        *fr_data;

	struct c2_db_cursor          fr_ptr;
};

struct c2_fol_rec_ops {
};

int  c2_fol_rec_lookup(struct c2_fol *fol, c2_fol_lsn_t lsn, 
		       struct c2_fol_rec *out);
void c2_fol_rec_fini  (struct c2_fol_rec *rec);

void c2_fol_rec_get(struct c2_fol_rec *rec);
void c2_fol_rec_put(struct c2_fol_rec *rec);

int  c2_fol_batch(struct c2_fol *fol, c2_fol_lsn_t lsn, uint32_t nr, 
		  struct c2_fol_rec *out);

/** @} end of fol group */

/* __COLIBRI_FOL_FOL_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
