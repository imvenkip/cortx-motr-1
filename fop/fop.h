/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_H__
#define __COLIBRI_FOP_FOP_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/list.h"
#include "addb/addb.h"
#include "fol/fol.h"

/**
   @defgroup fop File operation packet

   File Operation Packet (fop, struct c2_fop) is a description of particular
   file system or storage operation. fop code knows how to serialise a fop to
   the network wire or storage buffer, and how to de-serialise it back, which
   makes it functionally similar to ONCRPC XDR.

   A fop consists of fields. The structure and arrangement of a fop fields is
   described by a fop type (c2_fop_type), that also has a pointer to a set of
   operations that are used to manipulate the fop.

   The execution of an operation described by fop is carried out by a "fop
   machine" (fom, struct c2_fom).

   @note "Had I been one of the tragic bums who lurked in the mist of that
          station platform where a brittle young FOP was pacing back and forth,
          I would not have withstood the temptation to destroy him."

   @{
*/

/* import */
struct c2_fom;
struct c2_rpcmachine;
struct c2_service;
struct c2_fol;
struct c2_db_tx;

/* export */
struct c2_fop_type;
struct c2_fop_type_ops;
struct c2_fop_data;
struct c2_fop;
struct c2_fop_field;
struct c2_fop_field_type;
struct c2_fop_memlayout;
struct c2_fop_type_format;

typedef uint32_t c2_fop_type_code_t;

/**
   Type of a file system operation.

   There is an instance of c2_fop_type for "make directory" command, an instance
   for "write", "truncate", etc.
 */
struct c2_fop_type {
	/** Unique operation code. */
	c2_fop_type_code_t                ft_code;
	/** Operation name. */
	const char                       *ft_name;
	/** Linkage into a list of all known operations. */
	struct c2_list_link               ft_linkage;
	/** Type of a top level field in fops of this type. */
	struct c2_fop_field_type         *ft_top;
	const struct c2_fop_type_ops     *ft_ops;
	/** Format of this fop's top field. */
	struct c2_fop_type_format        *ft_fmt;
	struct c2_fol_rec_type            ft_rec_type;
	/**
	   ADDB context for events related to this fop type.
	 */
	struct c2_addb_ctx                ft_addb;
};

int  c2_fop_type_build(struct c2_fop_type *fopt);
void c2_fop_type_fini(struct c2_fop_type *fopt);

int  c2_fop_type_build_nr(struct c2_fop_type **fopt, int nr);
void c2_fop_type_fini_nr(struct c2_fop_type **fopt, int nr);

/**
   A context for fop processing in a service.

   A context is created by a service and passed to
   c2_fop_type_ops::fto_execute() as an argument. It is used to identify a
   particular fop execution in a service.
 */
struct c2_fop_ctx {
	struct c2_service *ft_service;
	/**
	   Service-dependent cookie identifying fop execution. Passed to
	   c2_service_ops::so_reply_post() to post a reply.

	   @see c2_net_reply_post()
	 */
	void              *fc_cookie;
};

/** fop type operations. */
struct c2_fop_type_ops {
	/** Create a fom that will carry out operation described by the fop. */
	int (*fto_fom_init)(struct c2_fop *fop, struct c2_fom **fom);
	/** XXX temporary entry point for threaded fop execution. */
	int (*fto_execute) (struct c2_fop *fop, struct c2_fop_ctx *ctx);
	/** fol record type operations for this fop type, or NULL is standard
	    operations are to be used. */
	const struct c2_fol_rec_type_ops  *fto_rec_ops;
};

/** 
    fop storage.

    A fop is stored in a buffer vector. XXX not for now.
 */
struct c2_fop_data {
	void            *fd_data;
};

/** fop. */
struct c2_fop {
	struct c2_fop_type *f_type;
	/** Pointer to the data where fop is serialised or will be
	    serialised. */
	struct c2_fop_data  f_data;
	/**
	   ADDB context for events related to this fop.
	 */
	struct c2_addb_ctx  f_addb;
};

struct c2_fop *c2_fop_alloc(struct c2_fop_type *fopt, void *data);
void           c2_fop_free(struct c2_fop *fop);
void          *c2_fop_data(struct c2_fop *fop);

/** True iff fop describes an operation that would mutate file system state when
    executed. */
bool c2_fop_is_update(const struct c2_fop_type *type);
/** True iff fop is a batch, i.e., contains other fops. */
bool c2_fop_is_batch (const struct c2_fop_type *type);

/**
   General class to which a fop field belongs.

   fop field kinds classify fop fields very broadly.
 */
enum c2_fop_field_kind {
	/**
	 * A fop field is "builtin" fop sub-system relies on the existence and
	 * functions of this field. A builtin field might be stored differently
	 * from other fields. For example, fields used for operation sequencing
	 * (slot and sequence identifiers) are interpreted by the rpc
	 * sub-system. To represent such fields uniformly, a special builtin fop
	 * field is introduced. Another example of built-in field is a field
	 * containing fop operation code.
	 */
	FFK_BUILTIN,
	/**
	   A fop field is standard if it belong to a small set of fields that
	   generic fop code can interpret. Examples of such fields are object,
	   lock, and other resource identifiers; security signatures, user
	   identifiers, etc.
	 */
	FFK_STANDARD,
	/**
	   Fields not belonging to the above kinds fall to this kind.
	 */
	FFK_OTHER,

	FFK_NR
};

/**
   fop field.
 */
struct c2_fop_field {
	/** Field name. */
	const char               *ff_name;
	struct c2_fop_field_type *ff_type;
	uint32_t                  ff_tag;
	void                    **ff_decor;
};


enum c2_fop_field_aggr {
	FFA_RECORD,
	FFA_UNION,
	FFA_SEQUENCE,
	FFA_TYPEDEF,
	FFA_ATOM,
	FFA_NR
};

enum c2_fop_field_primitive_type {
	FPF_VOID,
	FPF_BYTE,
	FPF_U32,
	FPF_U64,

	FPF_NR
};

/**
   fop field type in a programming language "type" sense.
 */
struct c2_fop_field_type {
	enum c2_fop_field_aggr   fft_aggr;
	const char              *fft_name;
	union {
		struct c2_fop_field_record {
		} u_record;
		struct c2_fop_field_union {
		} u_union;
		struct c2_fop_field_sequence {
			uint64_t s_max;
		} u_sequence;
		struct c2_fop_field_typedef {
		} u_typedef;
		struct c2_fop_field_atom {
			enum c2_fop_field_primitive_type a_type;
		} u_atom;
	} fft_u;
	/* a fop must be decorated, see any dictionary. */
	void                   **fft_decor;
	size_t                   fft_nr;
	struct c2_fop_field    **fft_child;
	struct c2_fop_memlayout *fft_layout;
};

void c2_fop_field_type_fini(struct c2_fop_field_type *t);

extern struct c2_fop_field_type C2_FOP_TYPE_VOID;
extern struct c2_fop_field_type C2_FOP_TYPE_BYTE;
extern struct c2_fop_field_type C2_FOP_TYPE_U32;
extern struct c2_fop_field_type C2_FOP_TYPE_U64;

int c2_fop_fol_rec_add(struct c2_fop *fop, struct c2_fol *fol, 
		       struct c2_db_tx *tx);

/**
   Contents of a given field in a given fop instance.

   @note a fop field can potentially have multiple values in the same fop. For
   example, an element field in the array field.
 */
struct c2_fop_field_instance {
	struct c2_fop       *ffi_fop;
	struct c2_fop_field *ffi_field;
	void                *ffi_val;
};


int  c2_fops_init(void);
void c2_fops_fini(void);

#include "fop/fop_format.h"

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_H__ */
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
