/* -*- C -*- */
/*
 * COPYRIGHT 2017 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy.Bilenko@seagate.com>
 * Original author: Dmitriy Podgorniy <Dmitriy.Podgorniy@seagate.com>
 * Original creation date: 29-Nov-2017
 */

#pragma once

#ifndef __MERO_BE_PAGED_H__
#define __MERO_BE_PAGED_H__

/**
 * @defgroup PageD
 *
 * @{
 */

#include "lib/mutex.h"
#include "lib/tlist.h"

#include "fop/fom.h"      /* m0_fom */

#include "be/tx_regmap.h" /* m0_be_reg_area */
#include "be/seg.h"       /* m0_be_reg */
#include "be/op.h"        /* m0_be_op */

struct m0_be_pd_request_queue;
struct m0_be_pd_request_pages;
struct m0_be_pd_request;
struct m0_be_reg_area;
struct m0_co_context;
struct m0_be_pd_fom;
struct m0_be_reg;
struct m0_be_op;
struct m0_fom;


struct m0_be_pD {
	struct m0_tl                   bp_mappings;

	/**
	 * NOTE: This queue may contain several subqueues, for example, for read
	 * and write requests. It can slightly improve performance for cases
	 * with blocked WRITE requests living in parallel with READ requests.
	 */
	struct m0_be_pd_request_queue *bp_reqq;
	struct m0_be_pd_fom           *bp_fom;
};

enum m0_be_pd_page_state {
	PPS_INIT,
	PPS_FINI,
	PPS_UNMAPPED,
	PPS_MAPPED,
	PPS_READING,
	PPS_READY,
	PPS_WRITING,
};

struct m0_be_pd_page {
	void                    *pp_page;
	void                    *pp_cellar;
	m0_bcount_t              pp_size;
	m0_bcount_t              pp_ref;
	bool                     pp_dirty;
	enum m0_be_pd_page_state pp_state;
	struct m0_mutex          pp_lock;
	struct m0_tlink          pp_pio_tlink;
};

struct m0_be_pd_mapping {
	struct m0_be_pd_page    *pas_pages;
	m0_bcount_t              pas_pcount;
	struct m0_be_pD         *pas_pd;
	struct m0_mutex          pas_lock;
	struct m0_tlink          pas_tlink;
	uint64_t                 pas_magic;
};


M0_INTERNAL void m0_be_pd_mappings_lock(struct m0_be_pD              *paged,
					struct m0_be_pd_request      *request);

M0_INTERNAL void m0_be_pd_mappings_unlock(struct m0_be_pD            *paged,
					  struct m0_be_pd_request    *request);

M0_INTERNAL bool m0_be_pd_page_is_in(struct m0_be_pD                 *paged,
				     struct m0_be_pd_page            *page);

/**
 * (struct m0_be_pD, struct m0_be_pd_pages)
 * M0_BE_PD_PAGES_FORALL(paged, page) {
 * }
 */
#define M0_BE_PD_PAGES_FORALL(paged, page)

M0_INTERNAL int m0_be_pd_mapping_init(struct m0_be_pD         *paged,
				      const void              *addr,
				      m0_bcount_t              size,
				      m0_bcount_t              page_size);

M0_INTERNAL int m0_be_pd_mapping_fini(struct m0_be_pD         *paged,
				      const void              *addr,
				      m0_bcount_t              size);

M0_INTERNAL int m0_be_pd_mapping_page_attach(struct m0_be_pd_mapping *mapping,
					struct m0_be_pd_page          *page);

M0_INTERNAL int m0_be_pd_mapping_page_detach(struct m0_be_pd_mapping *mapping,
					struct m0_be_pd_page          *page);

/* ------------------------------------------------------------------------- */

enum m0_be_pd_request_type {
	PRT_READ,
	PRT_WRITE,
};

// struct pages_iterator* to_pages(struct adapter*) {
// }
// struct adapter* to_adapter(struct pages_iterator*) {
// }

struct m0_be_pd_request_pages {
	enum m0_be_pd_request_type prp_type;
	struct m0_be_reg_area     *prp_reg_area; /* for M0_PRT_WRITE requests */
	struct m0_be_reg           prp_reg;      /* for  M0_PRT_READ requests */
};

/** XXX: rename to avoid init/and missing fini notation */
M0_INTERNAL void m0_be_pd_request_pages_init(struct m0_be_pd_request_pages *rqp,
					     enum m0_be_pd_request_type type,
					     struct m0_be_reg_area     *rarea,
					     struct m0_be_reg          *reg);

/**
 * Request encapsulates user-defined structures and decouple an abstraction from
 * its implementation so that the two can vary independently. Request implements
 * the bridge pattern which joins PD and BE-related structures. PD sees BE
 * requests through prism of its own structures, called (opaque) request and
 * (transparent) pages.
 *
 * Therefore, user sees and has to use only m0_be_pd_request_pages_init()
 * interface and internals of PD use M0_BE_PD_REQUEST_PAGES_FORALL() and
 * m0_be_pd_request__copy_to_cellars(), so that user and PD has not to bother
 * regarding how user structrues are converted into PD structures and how they
 * can be changed.
 *
 * @see https://en.wikipedia.org/wiki/Bridge_pattern
 *
 *   User FOM
 *   TX Grp FOM                                                 PD FOM
 *                       +-----------------------------------+
 * [reg | [reg]]     --> |            REQUEST                | --> [pages]
 *                       +-----------------------------------+
 * init(req,[reg|[reg]]) | +(PD)copy(paged, req)             | copy(req,...)
 *                       | +(PD)iterator(paged, req) : page* | iterator(req,...)
 *                       | +(U) init(req, [reg | [reg]])     |
 *                       +-----------------------------------+
 */
struct m0_be_pd_request {
	struct m0_be_op                prt_op;
	struct m0_be_pd_request_pages  prt_pages;
	struct m0_tlink                ptr_rq_link;
	uint64_t                       ptr_magic;
};

/* internal */
/* pd_request_pages iterator */
struct m0_be_prp_cursor {
	struct m0_be_pd_request_pages *rpi_pages;
	struct m0_be_pD               *rpi_paged;
	struct m0_be_pd_mapping       *rpi_mapping;
	struct m0_be_pd_page          *rpi_page;
	const void                    *rpi_addr;
	m0_bcount_t                    rpi_size;
};

M0_INTERNAL void m0_be_prp_cursor_init(struct m0_be_prp_cursor       *cursor,
				       struct m0_be_pD               *paged,
				       struct m0_be_pd_request_pages *pages,
				       const void                    *addr,
				       m0_bcount_t                    size);

M0_INTERNAL void m0_be_prp_cursor_fini(struct m0_be_prp_cursor *cursor);
M0_INTERNAL bool m0_be_prp_cursor_next(struct m0_be_prp_cursor *cursor);
M0_INTERNAL struct m0_be_pd_page *
m0_be_prp_cursor_page_get(struct m0_be_prp_cursor *cursor);

/**
 * (struct m0_be_pD*,
 *  struct m0_be_pd_request*,
 *  struct m0_be_pd_page*,
 *  const struct m0_be_reg_d*)
 * M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page, rd) {
 *	;
 * } M0_BE_PD_REQUEST_PAGES_ENDFOR;
 *
 * struct m0_be_pd_page *page;
 * struct m0_be_reg_d   *rd;
 *
 * M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page, rd) {
 *	copy_reg_to_page(page, rd);
 * } M0_BE_PD_REQUEST_PAGES_ENDFOR;
 *
 */
M0_INTERNAL void
m0_be_pd_request_pages_forall(struct m0_be_pD         *paged,
			      struct m0_be_pd_request *request,
			      bool (*iterate)(struct m0_be_pd_page *page,
					      struct m0_be_reg_d   *rd));

#define M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page, rd)		\
{									\
	struct m0_be_prp_cursor        cursor;				\
	struct m0_be_pd_request_pages *rpages = &(request)->prt_pages;	\
	struct m0_be_reg_d             rd_read;				\
									\
	if (rpages->prp_type == PRT_READ) {				\
		rd = &rd_read;						\
		rd->rd_reg.br_addr = rpages->prp_reg.br_addr;		\
		rd->rd_reg.br_size = rpages->prp_reg.br_size;		\
		goto read;						\
	}								\
									\
	M0_BE_REG_AREA_FORALL(rpages->prp_reg_area, rd) {		\
	read:								\
		m0_be_prp_cursor_init(&cursor, (paged), rpages,		\
				      rd->rd_reg.br_addr,		\
				      rd->rd_reg.br_size);		\
		while (m0_be_prp_cursor_next(&cursor)) {		\
			(page) = m0_be_prp_cursor_page_get(&cursor);

#define M0_BE_PD_REQUEST_PAGES_ENDFOR					\
		}							\
		m0_be_prp_cursor_fini(&cursor);				\
		if (rpages->prp_type == PRT_READ)			\
			break;						\
	}								\
} while (0)



/**
 * Copies data encapsulated inside request into cellar pages for write
 */
M0_INTERNAL void
m0_be_pd_request__copy_to_cellars(struct m0_be_pD         *paged,
				  struct m0_be_pd_request *request);

M0_INTERNAL bool m0_be_pd_pages_are_in(struct m0_be_pD         *paged,
				       struct m0_be_pd_request *request);

M0_INTERNAL void m0_be_pd_request_init(struct m0_be_pd_request       *request,
				       struct m0_be_pd_request_pages *pages);

M0_INTERNAL void m0_be_pd_request_fini(struct m0_be_pd_request       *request);

/* ------------------------------------------------------------------------- */

struct m0_be_pd_request_queue {
	struct m0_mutex prq_lock;
	struct m0_tl    prq_queue;
};

M0_INTERNAL int m0_be_pd_request_queue_init(struct m0_be_pd_request_queue *rq);
M0_INTERNAL void m0_be_pd_request_queue_fini(struct m0_be_pd_request_queue *rq);


M0_INTERNAL struct m0_be_pd_request *
m0_be_pd_request_queue_pop(struct m0_be_pd_request_queue *queue);

M0_INTERNAL void
m0_be_pd_request_queue_push(struct m0_be_pd_request_queue      *rqueue,
			    struct m0_be_pd_request            *request,
			    struct m0_fom                      *fom);

M0_INTERNAL void m0_be_pd_request_push(struct m0_be_pD         *paged,
				       struct m0_be_pd_request *request);

/* ------------------------------------------------------------------------- */

M0_INTERNAL void m0_be_pd_reg_get(struct m0_be_pD  *paged,
				  struct m0_be_reg *reg,
				  struct m0_be_op  *op);

M0_INTERNAL void m0_be_pd_reg_put(struct m0_be_pD        *paged,
				  const struct m0_be_reg *reg);

/* ------------------------------------------------------------------------- */

struct m0_be_pd_fom {
	struct m0_fom            bpf_gen;
	struct m0_reqh          *bpf_reqh;
	struct m0_be_pD         *bpf_pd;
	struct m0_be_pd_request *bpf_cur_request;
};

M0_INTERNAL void m0_be_pd_fom_init(struct m0_be_pd_fom    *fom,
				   struct m0_be_pD        *pd,
				   struct m0_reqh         *reqh);

M0_INTERNAL void m0_be_pd_fom_fini(struct m0_be_pd_fom    *fom);

M0_INTERNAL void m0_be_pd_fom_mod_init(void);
M0_INTERNAL void m0_be_pd_fom_mod_fini(void);

/* ------------------------------------------------------------------------- */

enum m0_be_pd_io_type {
	M0_PIT_READ,
	M0_PIT_WRITE,
};

struct m0_be_NEW_pd_io {
	int xxx;
};

M0_INTERNAL int m0_be_pd_io_init(struct m0_be_NEW_pd_io *pio,
				 struct m0_be_pD *paged,
				 enum m0_be_pd_io_type type);

M0_INTERNAL void m0_be_pd_io_fini(struct m0_be_NEW_pd_io *pio);
M0_INTERNAL struct m0_be_NEW_pd_io *m0_be_NEW_pd_io_get(struct m0_be_pD *paged);
M0_INTERNAL void m0_be_NEW_pd_io_put(struct m0_be_NEW_pd_io *pio);
M0_INTERNAL int m0_be_pd_io_launch(struct m0_be_NEW_pd_io *pio);
M0_INTERNAL void m0_be_NEW_pd_io_add(struct m0_be_NEW_pd_io *pio,
				 struct m0_be_pd_page *page);

/* ------------------------------------------------------------------------- */


/** @} end of PageD group */
#endif /* __MERO_BE_PAGED_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
