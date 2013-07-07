/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04-Jun-2013
 */

#include "db/db.h"
#include "cob/cob.h"
#include "reqh/reqh.h"
#include "fop/fom.h"
#include "fop/fom_simple.h"
#include "lib/misc.h"           /* m0_forall */
#include "lib/locality.h"
#include "ut/ut.h"

enum {
	NR = 4096,
	X_VALUE = 6
};

static bool                 passed[NR];
static uint64_t             core[NR];
static struct m0_mutex      lock;
static struct m0_semaphore  sem[NR];
static struct m0_sm_ast     ast[NR];
static struct m0_dbenv      dbenv;
static struct m0_cob_domain cob_dom;
static struct m0_fol        fol;
static struct m0_reqh       reqh;
static struct m0_fom_simple s;


static void _reqh_init(void)
{
	int result;

	result = m0_dbenv_init(&dbenv, "locality-test", 0);
	M0_UT_ASSERT(result == 0);
	result = m0_cob_domain_init(&cob_dom, &dbenv,
				    &(struct m0_cob_domain_id){ 1 });
	M0_UT_ASSERT(result == 0);

	result = M0_REQH_INIT(&reqh,
			      .rhia_dtm       = (void*)1,
			      .rhia_db        = &dbenv,
			      .rhia_mdstore   = (void*)1,
			      .rhia_fol       = &fol,
			      .rhia_svc       = (void*)1);
	M0_UT_ASSERT(result == 0);
	m0_reqh_start(&reqh);
}

static void _reqh_fini(void)
{
	m0_reqh_shutdown_wait(&reqh);
	m0_reqh_services_terminate(&reqh);
	m0_reqh_fini(&reqh);
	m0_cob_domain_fini(&cob_dom);
	m0_dbenv_fini(&dbenv);
}

static void _cb0(struct m0_sm_group *grp, struct m0_sm_ast *a)
{
	unsigned idx = a - ast;
	M0_UT_ASSERT(IS_IN_ARRAY(idx, ast));
	m0_mutex_lock(&lock);
	passed[idx] = true;
	core[m0_processor_id_get()]++;
	m0_mutex_unlock(&lock);
	m0_semaphore_up(&sem[idx]);
}

static int simple_tick(struct m0_fom *fom, int x, int *substate)
{
	static int expected = 0;

	M0_UT_ASSERT(x == X_VALUE);
	M0_UT_ASSERT(*substate == expected);

	expected++;
	if ((*substate)++ < NR)
		return M0_FSO_AGAIN;
	else {
		m0_semaphore_up(&sem[0]);
		return -1;
	}
}

void test_locality(void)
{
	unsigned             i;
	struct m0_bitmap     online;
	int                  result;
	size_t               here;

	m0_mutex_init(&lock);
	_reqh_init();
	for (i = 0; i < ARRAY_SIZE(sem); ++i) {
		m0_semaphore_init(&sem[i], 0);
		ast[i].sa_cb = &_cb0;
	}

	here = m0_locality_here()->lo_idx;
	m0_sm_ast_post(m0_locality_get(here)->lo_grp, &ast[0]);
	m0_semaphore_down(&sem[0]);
	M0_UT_ASSERT(passed[0]);
	passed[0] = false;
	M0_UT_ASSERT(m0_forall(j, ARRAY_SIZE(core), core[j] == !!(j == here)));
	core[here] = 0;

	for (i = 0; i < NR; ++i)
		m0_sm_ast_post(m0_locality_get(i)->lo_grp, &ast[i]);

	for (i = 0; i < NR; ++i)
		m0_semaphore_down(&sem[i]);

	result = m0_bitmap_init(&online, m0_processor_nr_max());
	M0_ASSERT(result == 0);
	m0_processors_online(&online);

	M0_UT_ASSERT(m0_forall(j, NR, passed[j]));
	M0_UT_ASSERT(m0_forall(j, ARRAY_SIZE(core),
			       (core[j] != 0) == (j < online.b_nr &&
						  m0_bitmap_get(&online, j))));

	M0_FOM_SIMPLE_POST(&s, &reqh, &simple_tick, X_VALUE, 1);
	m0_semaphore_down(&sem[0]);
	M0_UT_ASSERT(s.si_substate == NR + 1);

	for (i = 0; i < ARRAY_SIZE(sem); ++i)
		m0_semaphore_fini(&sem[i]);

	m0_bitmap_fini(&online);
	_reqh_fini();
	m0_mutex_fini(&lock);
}
M0_EXPORTED(test_locality);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
