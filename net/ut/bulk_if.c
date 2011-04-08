/* -*- C -*- */

#include "lib/arith.h"
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

/* params */
enum {
	UT_MAX_BUF_SIZE=4096,
	UT_MAX_BUF_SEGMENT_SIZE=2048,
	UT_MAX_BUF_SEGMENTS=4,
};
static bool ut_get_max_buffer_size_called = false;
static int ut_get_max_buffer_size(struct c2_net_domain *dom, c2_bcount_t *size)
{
	ut_get_max_buffer_size_called = true;
	*size = UT_MAX_BUF_SIZE;
	return 0;
}
static bool ut_get_max_buffer_segment_size_called = false;
static int ut_get_max_buffer_segment_size(struct c2_net_domain *dom, 
					  c2_bcount_t *size)
{
	ut_get_max_buffer_segment_size_called = true;
	*size = UT_MAX_BUF_SEGMENT_SIZE;
	return 0;
}
static bool ut_get_max_buffer_segments_called = false;
static int ut_get_max_buffer_segments(struct c2_net_domain *dom, 
				      int32_t *num_segs)
{
	ut_get_max_buffer_segments_called = true;
	*num_segs = UT_MAX_BUF_SEGMENTS;
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

static bool ut_buf_register_called = false;
static int ut_buf_register(struct c2_net_buffer *nb)
{
	ut_buf_register_called = true;
	return 0;
}

static bool ut_buf_deregister_called = false;
static int ut_buf_deregister(struct c2_net_buffer *nb)
{
	ut_buf_deregister_called = true;
	return 0;
}

static bool ut_buf_add_called = false;
static int ut_buf_add(struct c2_net_buffer *nb)
{
	ut_buf_add_called = true;
	return 0;
}

static bool ut_buf_del_called = false;
static int ut_buf_del(struct c2_net_buffer *nb)
{
	ut_buf_del_called = true;
	return 0;
}

struct ut_tm_pvt {
	struct c2_net_transfer_mc *tm;
};
static bool ut_tm_init_called = false;
static int ut_tm_init(struct c2_net_transfer_mc *tm)
{
	struct ut_tm_pvt *tmp;
	C2_ALLOC_PTR(tmp);
	tmp->tm = tm;
	tm->ntm_xprt_private = tmp;
	ut_tm_init_called = true;
	return 0;
}

static bool ut_tm_fini_called = false;
static int ut_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct ut_tm_pvt *tmp;
	tmp = tm->ntm_xprt_private;
	C2_ASSERT(tmp->tm == tm);
	c2_free(tmp);
	ut_tm_fini_called = true;
	return 0;
}

static bool ut_tm_start_called = false;
static int ut_tm_start(struct c2_net_transfer_mc *tm)
{
	ut_tm_start_called = true;
	/* may need to start a background thread */
	return 0;
}

static bool ut_tm_stop_called = false;
static int ut_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	ut_tm_stop_called = true;
	return 0;
}

static const struct c2_net_xprt_ops ut_xprt_ops = {
	.xo_dom_init                    = ut_dom_init,
	.xo_dom_fini                    = ut_dom_fini,
	.xo_get_max_buffer_size         = ut_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = ut_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = ut_get_max_buffer_segments,
	.xo_end_point_create            = ut_end_point_create,
	.xo_buf_register                = ut_buf_register,
	.xo_buf_deregister              = ut_buf_deregister,
	.xo_buf_add                     = ut_buf_add,
	.xo_buf_del                     = ut_buf_del,
	.xo_tm_init                     = ut_tm_init,
	.xo_tm_fini                     = ut_tm_fini,
	.xo_tm_start                    = ut_tm_start,
	.xo_tm_stop                     = ut_tm_stop,
};

static struct c2_net_xprt ut_xprt = {
	.nx_name = "ut/bulk",
	.nx_ops  = &ut_xprt_ops
};

/* utility subs */
static struct c2_net_buffer *
allocate_buffers(c2_bcount_t buf_size, 
		 c2_bcount_t buf_seg_size, 
		 int32_t buf_segs)
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
			sz = min64(256,buf_seg_size);
			nr = 1;
		}
		else {
			nr = buf_segs;
			if ((buf_size/buf_segs)>buf_seg_size) {
				sz = buf_seg_size;
				C2_ASSERT((sz * nr) <= buf_size);
			}
			else {
				sz = buf_size/buf_segs;
			}
		}
		rc = c2_bufvec_alloc(&nb->nb_buffer, nr, sz, 12);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == nr);
		C2_UT_ASSERT(c2_vec_count(&nb->nb_buffer.ov_vec) == (sz*nr));
	}

	return nbs;
}

void make_desc(struct c2_net_buf_desc *desc)
{
	static const char *p = "descriptor";
	size_t len = strlen(p)+1;
	desc->nbd_data = c2_alloc(len);
	desc->nbd_len = len;
	strcpy(desc->nbd_data, p);
}

/* callback subs */


/*
  Unit test starts
 */
void test_net_bulk_if(void)
{
	int rc, i;
	c2_bcount_t buf_size, buf_seg_size;
	int32_t   buf_segs;
	struct c2_net_domain *dom = &utdom;
	struct c2_net_buffer *nbs;
	struct c2_net_end_point *ep1, *ep2, *ep;
	struct c2_net_buf_desc d1, d2;


	C2_SET0(&d1);
	C2_SET0(&d2);
	make_desc(&d1);
	C2_UT_ASSERT(d1.nbd_data != NULL);
	C2_UT_ASSERT(d1.nbd_len > 0);
	rc = c2_net_desc_copy(&d1, &d2);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(d2.nbd_data != NULL);
	C2_UT_ASSERT(d2.nbd_len > 0);
	C2_UT_ASSERT(d1.nbd_data != d2.nbd_data);
	C2_UT_ASSERT(d1.nbd_len == d2.nbd_len);
	C2_UT_ASSERT(memcmp(d1.nbd_data, d2.nbd_data, d1.nbd_len) == 0);
	c2_net_desc_free(&d2);
	C2_UT_ASSERT(d2.nbd_data == NULL);
	C2_UT_ASSERT(d2.nbd_len == 0);

	/* initialize the domain */
	C2_UT_ASSERT(ut_dom_init_called == false);
	rc = c2_net_domain_init(dom, &ut_xprt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_dom_init_called);
	C2_UT_ASSERT(dom->nd_xprt == &ut_xprt);
	C2_UT_ASSERT(dom->nd_xprt_private == &ut_xprt_pvt);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));

	/* get max buffer size */
	C2_UT_ASSERT(ut_get_max_buffer_size_called == false);
	buf_size = 0;
	rc = c2_net_domain_get_max_buffer_size(dom, &buf_size);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_get_max_buffer_size_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_size == UT_MAX_BUF_SIZE);

	/* get max buffer segment size */
	C2_UT_ASSERT(ut_get_max_buffer_segment_size_called == false);
	buf_seg_size = 0;
	rc = c2_net_domain_get_max_buffer_segment_size(dom, &buf_seg_size);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_get_max_buffer_segment_size_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_seg_size == UT_MAX_BUF_SEGMENT_SIZE);

	/* get max buffer segments */
	C2_UT_ASSERT(ut_get_max_buffer_segments_called == false);
	buf_segs = 0;
	rc = c2_net_domain_get_max_buffer_segments(dom, &buf_segs);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_get_max_buffer_segments_called);
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
	nbs = allocate_buffers(buf_size, buf_seg_size, buf_segs);

	/* register the buffers */
	for(i=0; i < C2_NET_QT_NR; ++i){
		struct c2_net_buffer *nb;
		nb = &nbs[i];
		nb->nb_flags = 0;
		ut_buf_register_called = false;
		rc = c2_net_buffer_register(nb, dom);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(ut_buf_register_called);
		C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_REGISTERED);
	}


	/* TM init with callbacks */

	/* add MSG_RECV buf */

	/* TM start */

	/* wait on channel for started */

	/* initalize and add remaining types of buffers
	   use buffer private callbacks for the bulk
	 */

	/* fake each type of "post" response */

	/* add a buffer and fake del - check callback */

	/* free end points */
	rc = c2_net_end_point_put(ep2);
	C2_UT_ASSERT(rc == 0);

	/* de-register buffers */
	for(i=0; i < C2_NET_QT_NR; ++i){
		struct c2_net_buffer *nb;
		nb = &nbs[i];
		ut_buf_deregister_called = false;
		rc = c2_net_buffer_deregister(nb, dom);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(ut_buf_deregister_called);
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_REGISTERED));
	}

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
