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

#include "be/tx_regmap.h"  /* m0_be_reg_area */
#include "be/seg.h"        /* m0_be_reg */
#include "be/op.h"         /* m0_be_op */
#include "be/pd.h"         /* m0_be_pd_io_sched */

#include "module/module.h" /* m0_module */

struct m0_be_pd_request_queue;
struct m0_be_pd_request_pages;
struct m0_be_pd_request;
struct m0_be_reg_area;
struct m0_co_context;
struct m0_be_pd_fom;
struct m0_be_reg;
struct m0_be_op;
struct m0_fom;


enum m0_be_pd_module_level {
	M0_BE_PD_LEVEL_INIT,
	M0_BE_PD_LEVEL_IO_SCHED,
	M0_BE_PD_LEVEL_READY,
};

struct m0_be_pd_cfg {
	struct m0_be_pd_io_sched_cfg bpc_io_sched_cfg;
};

struct m0_be_pd {
	struct m0_be_pd_cfg            bp_cfg;
	struct m0_module               bp_module;
	struct m0_be_pd_io_sched       bp_io_sched;
	struct m0_tl                   bp_mappings;

	/**
	 * NOTE: This queue may contain several subqueues, for example, for read
	 * and write requests. It can slightly improve performance for cases
	 * with blocked WRITE requests living in parallel with READ requests.
	 */
	struct m0_be_pd_request_queue *bp_reqq;
	struct m0_be_pd_fom           *bp_fom;
};

M0_INTERNAL int m0_be_pd_init(struct m0_be_pd           *pd,
                              const struct m0_be_pd_cfg *pd_cfg);
M0_INTERNAL void m0_be_pd_fini(struct m0_be_pd *pd);

/**
 * @verbatim
 *
 *    +-> PPS_UNMAPPED -----------+
 *    |         |   ^             |
 *    |         |   +---+         |
 *    |         V       |         V
 *    +--- PPS_MAPPED   |      PPS_FINI
 *              |       |
 *              V       |
 *         PPS_READING  |
 *              |       |
 *              V       |
 *    +-->  PPS_READY --+
 *    |         |
 *    |         V
 *    +--- PPS_WRITING
 *
 * @endverbatim
 *
 * Future optimisation: make PPS_WRITING be allowed to transit to PPS_UNMAPPED
 * state. Synchronisation should be implemented via cellar pages and system
 * pages can be unmapped during writing if respective cellar pages remain
 * allocated.
 */
enum m0_be_pd_page_state {
	PPS_INIT, /* XXX remove? */
	PPS_FINI,
	PPS_UNMAPPED,
	PPS_MAPPED,
	PPS_READING,
	PPS_READY,
	PPS_WRITING,
};

/**
 * BE page is continuous memory region of a fixed size starting from
 * a fixed address. BE pages meet requirements: size is multiple of PAGE_SIZE,
 * starting address is multiple of the size.
 *
 * BE pages must not intersect.
 *
 * BE page can be in different states such as UNMAPPED, MAPPED, READY, READING,
 * WRITING. These states and their logic are controlled by other abstractions.
 * For example, mapping is responsible for transitions to UMMAPPED and MAPPED
 * states; paged FOM implements transition between READY, READING and WRITING
 * states.
 *
 * Cellar pages
 *
 * BE pages represent regions in some backing store. This knowledge and
 * semantic appears on higher level, but BE page contains reference to
 * respective cellar page as simplification. Cellar page is a partial copy
 * of BE page that equals to respective backing store region or value that
 * in synchronisation process with the region.
 *
 * Cellar pages are allocated on demand by user (paged FOM) and are either
 * memory buffers that can be written to the backing store via the stob
 * interface or pointers to mapped storage with memory-like access interface.
 *
 * Locking
 *
 * Users lock BE page in order to serialise state transitions.
 *
 * Semantics of states
 *
 * UNMAPPED state means that respective memory region must not be accessed
 * by users otherwise it can lead to an error or reading undefined value.
 *
 * In MAPPED state system pages are mapped for the memory region and it is
 * accessible. However, payload is not loaded from the backing store. Reading
 * memory region of the page in this state will produce zeros or poisoning
 * pattern.
 *
 * User grabs ownership to a page with transiting it to READING state. This
 * state indicates that the page is progress of reading payload from the backing
 * store. Reading the memory region will not cause errors, but result will be
 * undefined. The ownership is put with transition to the next state.
 *
 * In READY state the memory region is readable-writable by users.
 *
 * WRITING state indicates that page is in synchronisation with the backing
 * store. User performing synchronisation holds ownership to the page and other
 * users may not transit the page in other states. However, all users may read
 * and write to the memory region. This is because synchronisation is done using
 * cellar page. The user releases ownership by transiting the page to READY
 * state.
 *
 * FINI is the final state and object must not be accessed in this state.
 */
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

/**
 * TODO Name mapping with a proper term
 *
 * BE mapping is an abstraction which represents continuous memory region as
 * set of BE pages and implements mapping/unmapping logic for the page states
 * PPS_UNMAPPED and PPS_MAPPED.
 *
 * Mapping and unmapping
 *
 * BE mapping creates a single private anonymous mapping for the whole memory
 * region. System pages remain not resident and BE mapping provides interface
 * to populate system pages for a single BE page withing the BE mapping.
 *
 * Unmapping BE page is done via advising system that the system pages are not
 * needed anymore.
 *
 * BE mapping guarantees absence of page faults and segfaults during access to
 * memory region of a populated (attached) BE page.
 *
 * Caveats: with the above approach user can access memory region of a BE page
 * bypassing mapping interface. It will cause allocation of system pages and
 * such a user will not be caught.
 *
 * Locking
 *
 * TODO
 */
struct m0_be_pd_mapping {
	struct m0_be_pd_page    *pas_pages;
	m0_bcount_t              pas_pcount;
	struct m0_be_pd         *pas_pd;
	struct m0_mutex          pas_lock;
	struct m0_tlink          pas_tlink;
	uint64_t                 pas_magic;
};


M0_INTERNAL void m0_be_pd_mappings_lock(struct m0_be_pd              *paged,
					struct m0_be_pd_request      *request);

M0_INTERNAL void m0_be_pd_mappings_unlock(struct m0_be_pd            *paged,
					  struct m0_be_pd_request    *request);

M0_INTERNAL bool m0_be_pd_page_is_in(struct m0_be_pd                 *paged,
				     struct m0_be_pd_page            *page);

/**
 * (struct m0_be_pd, struct m0_be_pd_pages)
 * M0_BE_PD_PAGES_FORALL(paged, page) {
 * }
 */
#define M0_BE_PD_PAGES_FORALL(paged, page)

M0_INTERNAL int m0_be_pd_mapping_init(struct m0_be_pd *paged,
				      void            *addr,
				      m0_bcount_t      size,
				      m0_bcount_t      page_size);

M0_INTERNAL int m0_be_pd_mapping_fini(struct m0_be_pd         *paged,
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

/**
 * Keeps track of structures that user provides during WRITE and READ requests
 * which are converted somehow into paged m0_be_pd_page-s. For paged the content
 * of m0_be_pd_request_pages has to remain opaque(!).
 * @note Absence of any aggregated structure having "*page*" expression inside
 * may be missleading therefore this note is here. PageD treats this structure
 * as opaque structure without any knowledge of its contents.
 * @see m0_be_pd_request and explanation for more details.
 */
struct m0_be_pd_request_pages {
	enum m0_be_pd_request_type prp_type;
	struct m0_be_reg_area     *prp_reg_area; /* for M0_PRT_WRITE requests */
	struct m0_be_reg           prp_reg;      /* for  M0_PRT_READ requests */
};

/**
 * Fills request pages with data, related to READ and WRITE requests.
 *
 * @param type  PRT_READ or PRT_WRITE request
 * @param rarea reg area, provided by the user for WRITE request
 * @param reg   reg,      provided by the user for READ  request
 */
M0_INTERNAL void m0_be_pd_request_pages_fill(struct m0_be_pd_request_pages *rqp,
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

/**
 * Provide means to iterate over m0_be_pd_request_pages structure by page,
 * containg an arbitary region or its part, defined by @addr and @size
 * parameters. Used internally to implement paged logic.
 */
struct m0_be_prp_cursor {
	/** Iterable structure */
	struct m0_be_pd_request_pages *rpi_pages;
	/** Paged */
	struct m0_be_pd               *rpi_paged;

	/** Last seen by the iterator mapping, where last iterated page lives */
	struct m0_be_pd_mapping       *rpi_mapping;
	/** Last seen page, where the region (addr, size) lives */
	struct m0_be_pd_page          *rpi_page;

	/** Iterated region start address */
	const void                    *rpi_addr;
	/** Iterated region size */
	m0_bcount_t                    rpi_size;
};

/**
 * Initializes m0_be_prp_cursor
 *
 * @param pages a part of request iteration over which pages is beeing performed
 * @param addr iterated region start address
 * @param size iterated region size
 */
M0_INTERNAL void m0_be_prp_cursor_init(struct m0_be_prp_cursor       *cursor,
				       struct m0_be_pd               *paged,
				       struct m0_be_pd_request_pages *pages,
				       const void                    *addr,
				       m0_bcount_t                    size);

M0_INTERNAL void m0_be_prp_cursor_fini(struct m0_be_prp_cursor *cursor);

/**
 * Moves @cursor over it's next position w.r.t. retreive next page.
 * @return false if all pages're iterated, else true
 */
M0_INTERNAL bool m0_be_prp_cursor_next(struct m0_be_prp_cursor *cursor);

/**
 * Retrieves page pointer from given @cursor
 * @return NULL if there's no valid pages inside, else a pointer to the page
 * inside paged mappings.
 */
M0_INTERNAL struct m0_be_pd_page *
m0_be_prp_cursor_page_get(struct m0_be_prp_cursor *cursor);

/**
 * Iterates over region of m0_be_pd_request and corresponding to this region
 * pages inside paged mappings.
 *
 * @param paged   given paged.
 * @param request READ or WRITE request obtained by paged.
 * @param iterate function callback into which current page and current iterated
 *                region is being passed.
 */
M0_INTERNAL void
m0_be_pd_request_pages_forall(struct m0_be_pd         *paged,
			      struct m0_be_pd_request *request,
			      bool (*iterate)(struct m0_be_pd_page *page,
					      struct m0_be_reg_d   *rd));

/**
 * Iterates over region of m0_be_pd_request and corresponding to this region
 * pages inside paged mappings.
 *
 * @param paged   given paged.
 * @param request READ or WRITE request obtained by paged.
 * @param page    iterated page
 * @param rd      iterated region
 * @code
 * struct m0_be_pd_page *page;
 * struct m0_be_reg_d   *rd;
 *
 * M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page, rd) {
 *	copy_reg_to_page(page, rd);
 * } M0_BE_PD_REQUEST_PAGES_ENDFOR;
 * @endcode
 */
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
 * Copies data encapsulated inside request into cellar pages for WRITE.
 *
 * @param paged   given paged.
 * @param request READ or WRITE request obtained by paged.
 */
M0_INTERNAL void
m0_be_pd_request__copy_to_cellars(struct m0_be_pd         *paged,
				  struct m0_be_pd_request *request);

/**
 * @return true if all requested by @request pages are "resident" inside @paged
 * mappings and ready to be used by READ requests.
 */
M0_INTERNAL bool m0_be_pd_pages_are_in(struct m0_be_pd         *paged,
				       struct m0_be_pd_request *request);

M0_INTERNAL void m0_be_pd_request_init(struct m0_be_pd_request       *request,
				       struct m0_be_pd_request_pages *pages);

M0_INTERNAL void m0_be_pd_request_fini(struct m0_be_pd_request       *request);

/* ------------------------------------------------------------------------- */

/**
 * Request queue is a structure which stores all incoming paged requests for
 * processing. Current implementation provides just generic features but in the
 * future, it's implementation is going to be replaced by something which knows
 * conceptions like "dispatch priority" or "requests that can be processed in
 * parallel".
 */
struct m0_be_pd_request_queue {
	struct m0_mutex prq_lock;
	struct m0_tl    prq_queue;
};

M0_INTERNAL int m0_be_pd_request_queue_init(struct m0_be_pd_request_queue *rq);
M0_INTERNAL void m0_be_pd_request_queue_fini(struct m0_be_pd_request_queue *rq);

/**
 * Internal interface. Pops the first available request from request queue.
 * @return NULL if request queue is empty, otherwise a valid request.
 */
M0_INTERNAL struct m0_be_pd_request *
m0_be_pd_request_queue_pop(struct m0_be_pd_request_queue *rqueue);

/**
 * Internal interface. Pushes given @request into processing request queue
 * @rqueue. If needed, wakes up processing PD FOM which consumes requests from
 * request queue.
 */
M0_INTERNAL void
m0_be_pd_request_queue_push(struct m0_be_pd_request_queue      *rqueue,
			    struct m0_be_pd_request            *request,
			    struct m0_fom                      *fom);

/**
 * External interface which is used outside paged machinery. Puts a @request
 * into processing of given @paged.
 */
M0_INTERNAL void m0_be_pd_request_push(struct m0_be_pd         *paged,
				       struct m0_be_pd_request *request);

/* ------------------------------------------------------------------------- */

M0_INTERNAL void m0_be_pd_reg_get(struct m0_be_pd  *paged,
				  struct m0_be_reg *reg,
				  struct m0_be_op  *op);

M0_INTERNAL void m0_be_pd_reg_put(struct m0_be_pd        *paged,
				  const struct m0_be_reg *reg);

/* ------------------------------------------------------------------------- */

struct m0_be_pd_fom {
	struct m0_fom            bpf_gen;
	struct m0_reqh          *bpf_reqh;
	struct m0_be_pd         *bpf_pd;
	struct m0_be_pd_request *bpf_cur_request;
};

M0_INTERNAL void m0_be_pd_fom_init(struct m0_be_pd_fom    *fom,
				   struct m0_be_pd        *pd,
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
				 struct m0_be_pd *paged,
				 enum m0_be_pd_io_type type);

M0_INTERNAL void m0_be_pd_io_fini(struct m0_be_NEW_pd_io *pio);
M0_INTERNAL struct m0_be_NEW_pd_io *m0_be_NEW_pd_io_get(struct m0_be_pd *paged);
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
