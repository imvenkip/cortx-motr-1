/* -*- C -*- */

/*
 * WARNING: this file is automatically generated
 * from fom_io.ff by fop2c. Do not edit.
 */

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "fom_io_k.h"
#include <linux/module.h>

struct c2_fop_memlayout c2_fom_fop_fid_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_fop_fid),
	.fm_child = {
		{ offsetof(struct c2_fom_fop_fid, f_seq) },
		{ offsetof(struct c2_fom_fop_fid, f_oid) },
	}
};

struct c2_fop_memlayout c2_fom_io_seg_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_seg),
	.fm_child = {
		{ offsetof(struct c2_fom_io_seg, f_offset) },
		{ offsetof(struct c2_fom_io_seg, f_count) },
	}
};

struct c2_fop_memlayout c2_fom_io_buf_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_buf),
	.fm_child = {
		{ offsetof(struct c2_fom_io_buf, cib_count) },
		{ offsetof(struct c2_fom_io_buf, cib_value) },
	}
};

struct c2_fop_memlayout c2_fom_io_vec_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_vec),
	.fm_child = {
		{ offsetof(struct c2_fom_io_vec, civ_count) },
		{ offsetof(struct c2_fom_io_vec, civ_seg) },
	}
};

struct c2_fop_memlayout c2_fom_io_write_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_write),
	.fm_child = {
		{ offsetof(struct c2_fom_io_write, siw_object) },
		{ offsetof(struct c2_fom_io_write, siw_offset) },
		{ offsetof(struct c2_fom_io_write, siw_buf) },
	}
};

struct c2_fop_memlayout c2_fom_io_write_rep_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_write_rep),
	.fm_child = {
		{ offsetof(struct c2_fom_io_write_rep, siwr_rc) },
		{ offsetof(struct c2_fom_io_write_rep, siwr_count) },
	}
};

struct c2_fop_memlayout c2_fom_io_read_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_read),
	.fm_child = {
		{ offsetof(struct c2_fom_io_read, sir_object) },
		{ offsetof(struct c2_fom_io_read, sir_seg) },
	}
};

struct c2_fop_memlayout c2_fom_io_read_rep_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_read_rep),
	.fm_child = {
		{ offsetof(struct c2_fom_io_read_rep, sirr_rc) },
		{ offsetof(struct c2_fom_io_read_rep, sirr_buf) },
	}
};

struct c2_fop_memlayout c2_fom_io_create_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_create),
	.fm_child = {
		{ offsetof(struct c2_fom_io_create, sic_object) },
	}
};

struct c2_fop_memlayout c2_fom_io_create_rep_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_create_rep),
	.fm_child = {
		{ offsetof(struct c2_fom_io_create_rep, sicr_rc) },
	}
};

struct c2_fop_memlayout c2_fom_io_quit_memlayout = {
	.fm_sizeof = sizeof (struct c2_fom_io_quit),
	.fm_child = {
		{ offsetof(struct c2_fom_io_quit, siq_rc) },
	}
};



/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
