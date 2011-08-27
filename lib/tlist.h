/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/26/2011
 */

#ifndef __COLIBRI_LIB_TLIST_H__
#define __COLIBRI_LIB_TLIST_H__

#include "lib/list.h"
#include "lib/types.h"                    /* prtdiff_t, uint64_t */

/**
   @defgroup tlist Typed lists.
   @{
 */

struct c2_tl_descr;
struct c2_tl;

struct c2_tl_descr {
	const char *td_name;
	ptrdiff_t   td_offset;
	ptrdiff_t   td_magic_offset;
	uint64_t    td_head_magic;
	uint64_t    td_link_magic;
};

struct c2_tl {
	uint64_t       t_magic;
	struct c2_list t_head;
};

struct c2_tlink {
	struct c2_list_link t_link;
};

void c2_tlist_init(const struct c2_tl_descr *d, struct c2_tl *list);
void c2_tlist_fini(const struct c2_tl_descr *d, struct c2_tl *list);

void c2_tlink_init(const struct c2_tl_descr *d, void *obj);
void c2_tlink_fini(const struct c2_tl_descr *d, void *obj);

bool c2_tlist_invariant(const struct c2_tl_descr *d, const struct c2_tl *list);
bool c2_tlink_invariant(const struct c2_tl_descr *d, const void *obj);

bool   c2_tlist_is_empty(const struct c2_tl_descr *d, const struct c2_tl *list);
bool   c2_tlist_is_in   (const struct c2_tl_descr *d,
			 const struct c2_tl_descr *d, const void *obj);

bool   c2_tlist_contains(const struct c2_tl_descr *d, const struct c2_tl *list,
			 const void *obj);
size_t c2_tlist_length(const struct c2_tl_descr *d,
		       const struct c2_tlist *list);
void   c2_tlist_add(const struct c2_tl_descr *d,
		    struct c2_tlist *list, void *obj);
void   c2_tlist_add_tail(const struct c2_tl_descr *d,
			 struct c2_tlist *list, void *obj);
void   c2_tlist_add_after(const struct c2_tl_descr *d, void *obj, void *new);
void   c2_tlist_add_before(const struct c2_tl_descr *d, void *obj, void *new);
bool   c2_tlist_del(const struct c2_tl_descr *d, void *obj);
void   c2_tlist_move(const struct c2_tl_descr *d,
		     struct c2_tlist *list, void *obj);
void   c2_tlist_move_tail(const struct c2_tl_descr *d,
			  struct c2_tlist *list, void *obj);
void  *c2_tlist_head(const struct c2_tl_descr *d, struct c2_tlist *list);
void  *c2_tlist_tail(const struct c2_tl_descr *d, struct c2_tlist *list);
void  *c2_tlist_next(const struct c2_tl_descr *d, struct c2_tlist *list,
		     void *obj);
void  *c2_tlist_prev(const struct c2_tl_descr *d, struct c2_tlist *list,
		     void *obj);

void *c2_tlist_next_safe(const struct c2_tl_descr *d, struct c2_tlist *list,
			 void *obj);

#define c2_tlist_for_each(d, head, obj)			\
	for (obj = c2_tlist_head(d, head); obj != NULL;	\
	     obj = c2_tlist_next(d, head, obj))

#define c2_tlist_for_each_safe(d, head, obj, tmp)		\
	for (obj = c2_tlist_head(d, head),			\
	     tmp = c2_tlist_next_safe(d, head, obj);		\
	     obj != NULL;					\
	     obj = tmp, tmp = c2_tlist_next_safe(d, head, tmp))

/** @} end of tlist group */

/* __COLIBRI_LIB_TLIST_H__ */
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
