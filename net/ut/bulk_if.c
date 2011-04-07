/* -*- C -*- */

#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/mutex.h"
#include "lib/ut.h"

#include "net/net.h"

static struct c2_net_domain utdom;

/*
 *****************************************************
   Fake transport for the UT 
 *****************************************************
*/
static struct {
	int num;
} ut_xprt_pvt;

static bool ut_dom_init_called=false;
static int ut_dom_init(struct c2_net_xprt *xprt,
		       struct c2_net_domain *dom)
{
	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_dom_init_called = true;
	dom->nd_xprt_private = &ut_xprt_pvt;
	return 0;
}

static bool ut_dom_fini_called=false;
static void ut_dom_fini(struct c2_net_domain *dom)
{
	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_dom_fini_called = true;
}

enum {
	UT_MAX_BUF_SIZE=4096,
	UT_MAX_BUF_SEGMENTS=4,
};
static bool ut_param_get_called = false;
static int ut_param_get(struct c2_net_domain *dom,
			int param, va_list varargs)
{
	c2_bcount_t *bc;
	int32_t *i32;

	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_param_get_called = true;
	switch ( param ) {
	case C2_NET_PARAM_MAX_BUFFER_SIZE:
		bc = va_arg(varargs, c2_bcount_t *);
		*bc= UT_MAX_BUF_SIZE;
		break;
	case C2_NET_PARAM_MAX_BUFFER_SEGMENTS:
		i32 = va_arg(varargs, int32_t *);
		*i32= UT_MAX_BUF_SEGMENTS;
		break;
	default:
		return -ENOENT;
	}
	return 0;
}

struct ut_ep {
	char *addr;
	struct c2_net_end_point uep;
};
static bool ut_end_point_release_called = false;
static struct c2_net_end_point *ut_last_ep_released;
static void ut_end_point_release(struct c2_ref *ref)
{
	struct c2_net_end_point *ep;
	struct ut_ep *utep;
	struct c2_net_domain *dom;
	ut_end_point_release_called = true;
	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	ut_last_ep_released = ep;
	dom = ep->nep_dom;
	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	c2_list_del(&ep->nep_dom_linkage);
	ep->nep_dom = NULL;
	utep = container_of(ep, struct ut_ep, uep);
	c2_free(utep);
}
static bool ut_end_point_create_called = false;
static int ut_end_point_create(struct c2_net_end_point **epp,
			       struct c2_net_domain *dom,
			       va_list varargs)
{
	char *ap;
	struct ut_ep *utep;
	struct c2_net_end_point *ep;

	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_end_point_create_called = true;
	ap = va_arg(varargs, char *);
	if ( ap == 0 ) {
		/* end of args - don't support dynamic */
		return -ENOSYS;
	}
	/* check if its already on the domain list */
	c2_list_for_each_entry(&dom->nd_end_points, ep,
			       struct c2_net_end_point,
			       nep_dom_linkage) {
		utep = container_of(ep, struct ut_ep, uep);
		if ( strcmp(utep->addr, ap) == 0 ) {
			c2_ref_get(&ep->nep_ref); /* refcnt++ */
			*epp = ep;
			return 0;
		}
	}
	/* allocate a new end point */
	C2_ALLOC_PTR(utep);
	utep->addr = ap; /* avoid strdup; this is a ut! */
	c2_ref_init(&utep->uep.nep_ref, 1, ut_end_point_release);
	utep->uep.nep_dom = dom;
	c2_list_link_init(&utep->uep.nep_dom_linkage);
	c2_list_add_tail(&dom->nd_end_points, &utep->uep.nep_dom_linkage);
	*epp = &utep->uep;
	return 0;
}

static const struct c2_net_xprt_ops ut_xprt_ops = {
	.xo_dom_init         = ut_dom_init,
	.xo_dom_fini         = ut_dom_fini,
	.xo_param_get        = ut_param_get,
	.xo_end_point_create = ut_end_point_create,
};

static struct c2_net_xprt ut_xprt = {
	.nx_name = "ut/bulk",
	.nx_ops  = &ut_xprt_ops
};

/* utility subs */
static struct c2_net_buffer *
allocate_buffers(c2_bcount_t buf_size, int32_t buf_segs)
{
	int rc;
	int i;
	struct c2_net_buffer *nbs;
	struct c2_net_buffer *nb;
	c2_bcount_t sz;
	int32_t nr;

	C2_ALLOC_ARR(nbs, C2_NET_QT_NR);
	for(i=0; i<C2_NET_QT_NR; i++) {
		nb = &nbs[i];
		C2_SET0(nb);
		if ( i == C2_NET_QT_MSG_RECV ) {
			sz = 256;
			nr = 1;
		}
		else {
			sz = buf_size;
			nr = buf_segs;
		}
		rc = c2_bufvec_alloc(&nb->nb_buffer, nr, sz, 12);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == nr);
		C2_UT_ASSERT(c2_vec_count(&nb->nb_buffer.ov_vec) == (sz*nr));
	}

	return nbs;
}

/*
  Unit test starts
 */
void test_net_bulk_if(void)
{
	int rc;
	c2_bcount_t buf_size;
	int32_t   buf_segs;
	struct c2_net_domain *dom = &utdom;
	struct c2_net_buffer *nbs;
	struct c2_net_end_point *ep1, *ep2, *ep;

	/* initialize the domain */
	C2_UT_ASSERT(ut_dom_init_called == false);
	rc = c2_net_domain_init(dom, &ut_xprt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_dom_init_called);
	C2_UT_ASSERT(dom->nd_xprt == &ut_xprt);
	C2_UT_ASSERT(dom->nd_xprt_private == &ut_xprt_pvt);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));

	/* get max buffer size */
	C2_UT_ASSERT(ut_param_get_called == false);
	buf_size = 0;
	rc = c2_net_domain_get_max_buffer_size(dom, &buf_size);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_param_get_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_size == UT_MAX_BUF_SIZE);

	/* get max buffer segments */
	ut_param_get_called = false;
	buf_segs = 0;
	rc = c2_net_domain_get_max_buffer_segments(dom, &buf_segs);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_param_get_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_segs == UT_MAX_BUF_SEGMENTS);

	/* Test desired end point behavior
	   A real transport isn't actually forced to maintain
	   reference counts this way, but ought to do so.
	 */
	C2_UT_ASSERT(ut_end_point_create_called == false);
	rc = c2_net_end_point_create(&ep1, dom, 0);
	C2_UT_ASSERT(rc != 0); /* no dynamic */
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_ASSERT(c2_list_is_empty(&dom->nd_end_points));

	ut_end_point_create_called = false;
	rc = c2_net_end_point_create(&ep1, dom, "addr1", 0);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_ASSERT(!c2_list_is_empty(&dom->nd_end_points));
	C2_UT_ASSERT(c2_atomic64_get(&(ep1->nep_ref.ref_cnt)) == 1);

	rc = c2_net_end_point_create(&ep2, dom, "addr2", 0);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ep2 != ep1);

	rc = c2_net_end_point_create(&ep, dom, "addr1", 0);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ep == ep1);
	C2_UT_ASSERT(c2_atomic64_get(&(ep->nep_ref.ref_cnt)) == 2);

	C2_UT_ASSERT(ut_end_point_release_called == false);
	rc = c2_net_end_point_get(ep); /* refcnt=3 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_atomic64_get(&(ep->nep_ref.ref_cnt)) == 3);

	C2_UT_ASSERT(ut_end_point_release_called == false);
	rc = c2_net_end_point_put(ep); /* refcnt=2 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_release_called == false);
	C2_UT_ASSERT(c2_atomic64_get(&(ep->nep_ref.ref_cnt)) == 2);

	rc = c2_net_end_point_put(ep); /* refcnt=1 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_release_called == false);
	C2_UT_ASSERT(c2_atomic64_get(&(ep->nep_ref.ref_cnt)) == 1);

	rc = c2_net_end_point_put(ep); /* refcnt=0 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_release_called);
	C2_UT_ASSERT(ut_last_ep_released == ep);
	ep1 = NULL; /* not valid! */

	/* allocate buffers for testing */
	nbs = allocate_buffers(buf_size, buf_segs);

	/* free end points */
	rc = c2_net_end_point_put(ep2);
	C2_UT_ASSERT(rc == 0);

	/* fini the domain */
	C2_UT_ASSERT(ut_dom_fini_called == false);
	c2_net_domain_fini(dom);
	C2_UT_ASSERT(ut_dom_fini_called);

}

const struct c2_test_suite net_bulk_if_ut = {
        .ts_name = "net-bulk-if",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_if", test_net_bulk_if },
                { NULL, NULL }
        }
};

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
