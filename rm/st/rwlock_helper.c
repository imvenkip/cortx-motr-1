#include <unistd.h>

#include "lib/trace.h"
#include "lib/user_space/getopts.h"
#include "mero/init.h"
#include "module/instance.h"
#include "rm/rm.h"
#include "rm/rm_rwlock.h"
#include "rm/rm_service.h"
#include "rpc/rpclib.h"        /* m0_rpc_client_connect */

struct wlock_ctx {
	struct m0_rpc_machine     *wlc_rmach;     /**< rpc machine            */
	struct m0_rpc_conn         wlc_conn;      /**< rpc connection         */
	struct m0_rpc_session      wlc_sess;      /**< rpc session            */
	char                      *wlc_rm_addr;   /**< HA-reported RM address */
	struct m0_fid              wlc_rm_fid;    /**< HA-reported RM fid     */
	struct m0_rw_lockable      wlc_rwlock;    /**< lockable resource      */
	struct m0_rm_owner         wlc_owner;     /**< local owner-borrower   */
	struct m0_fid              wlc_owner_fid; /**< owner fid              */
	struct m0_rm_remote        wlc_creditor;  /**< remote creditor        */
	struct m0_rm_incoming      wlc_req;       /**< request to wait on     */
	/** semaphore to wait until request is completed */
	struct m0_semaphore        wlc_sem;
	/**
	 * Write resource domain. Needs to be separate from global read domain
	 * used by @ref rconfc instances. (see m0_rwlockable_read_domain())
	 */
	struct m0_rm_domain        wlc_dom;
	/**
	 * Write resource type. Needs to be registered with the write resource
	 * domain.
	 */
	struct m0_rm_resource_type wlc_rt;
	/** result code of write lock request */
	int32_t                    wlc_rc;
} wlx;

static void write_lock_complete(struct m0_rm_incoming *in,
				int32_t                rc)
{
	M0_ENTRY("incoming %p, rc %d", in, rc);
	wlx.wlc_rc = rc;
	m0_semaphore_up(&wlx.wlc_sem);
	M0_LEAVE();
}

static void write_lock_conflict(struct m0_rm_incoming *in)
{
	/* Do nothing */
}

static struct m0_rm_incoming_ops ri_ops = {
	.rio_complete = write_lock_complete,
	.rio_conflict = write_lock_conflict,
};

static int wlock_ctx_create(struct m0_rpc_machine *rpc_mach, const char *rm_ep)
{
	int                        rc;

	wlx.wlc_rmach = rpc_mach;
	m0_rwlockable_domain_type_init(&wlx.wlc_dom, &wlx.wlc_rt);
	m0_rw_lockable_init(&wlx.wlc_rwlock, &M0_RWLOCK_FID, &wlx.wlc_dom);
	m0_fid_tgenerate(&wlx.wlc_owner_fid, M0_RM_OWNER_FT);
	m0_rm_rwlock_owner_init(&wlx.wlc_owner, &wlx.wlc_owner_fid,
				&wlx.wlc_rwlock, NULL);
	wlx.wlc_rm_addr = m0_strdup(rm_ep);
	rc = m0_semaphore_init(&wlx.wlc_sem, 0);
	return M0_RC(rc);
}

static int wlock_ctx_connect(struct wlock_ctx *wlx)
{
	enum { MAX_RPCS_IN_FLIGHT = 15 };

	M0_PRE(wlx != NULL);
	return m0_rpc_client_connect(&wlx->wlc_conn, &wlx->wlc_sess,
				     wlx->wlc_rmach, wlx->wlc_rm_addr, NULL,
				     MAX_RPCS_IN_FLIGHT);
}

static void wlock_ctx_creditor_setup(struct wlock_ctx *wlx)
{
	struct m0_rm_owner    *owner;
	struct m0_rm_remote   *creditor;

	M0_PRE(wlx != NULL);
	M0_ENTRY("wlx = %p", wlx);
	owner = &wlx->wlc_owner;
	creditor = &wlx->wlc_creditor;
	m0_rm_remote_init(creditor, owner->ro_resource);
	creditor->rem_session = &wlx->wlc_sess;
	m0_rm_owner_creditor_reset(owner, creditor);
	M0_LEAVE();
}

static void _write_lock_get(struct wlock_ctx *wlx)
{
	struct m0_rm_incoming *req;

	M0_PRE(wlx != NULL);
	M0_ENTRY("wlock ctx = %p", wlx);
	req = &wlx->wlc_req;
	m0_rm_rwlock_req_init(req, &wlx->wlc_owner, &ri_ops,
			      RIF_MAY_BORROW | RIF_MAY_REVOKE | RIF_LOCAL_WAIT,
			      RM_RWLOCK_WRITE);
	m0_rm_credit_get(req);
	M0_LEAVE();
}

static void wlock_ctx_destroy(struct wlock_ctx *wlx)
{
	int rc;

	M0_PRE(wlx != NULL);

	M0_ENTRY("wlock ctx %p", wlx);
	m0_rm_owner_windup(&wlx->wlc_owner);
	rc = m0_rm_owner_timedwait(&wlx->wlc_owner,
				   M0_BITS(ROS_FINAL, ROS_INSOLVENT),
				   M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "owner winduped");
	m0_rm_rwlock_owner_fini(&wlx->wlc_owner);
	m0_rw_lockable_fini(&wlx->wlc_rwlock);
	m0_rwlockable_domain_type_fini(&wlx->wlc_dom, &wlx->wlc_rt);
	M0_LEAVE();
}

static void wlock_ctx_disconnect(struct wlock_ctx *wlx)
{
	int               rc;

	M0_PRE(_0C(wlx != NULL) && _0C(!M0_IS0(&wlx->wlc_sess)));
	M0_ENTRY("wlock ctx %p", wlx);
	rc = m0_rpc_session_destroy(&wlx->wlc_sess, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy wlock session");
	rc = m0_rpc_conn_destroy(&wlx->wlc_conn, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy wlock connection");
	M0_LEAVE();

}

static void wlock_ctx_creditor_unset(struct wlock_ctx *wlx)
{
	M0_PRE(wlx != NULL);
	M0_ENTRY("wlx = %p", wlx);
	m0_rm_remote_fini(&wlx->wlc_creditor);
	wlx->wlc_owner.ro_creditor = NULL;
	M0_LEAVE();
}

static void _write_lock_put(struct wlock_ctx *wlx)
{
	struct m0_rm_incoming *req;

	M0_PRE(wlx != NULL);
	M0_ENTRY("wlock ctx = %p", wlx);
	req = &wlx->wlc_req;
	m0_rm_credit_put(req);
	m0_rm_incoming_fini(req);
	wlock_ctx_creditor_unset(wlx);
	M0_LEAVE();
}

static void rm_write_lock_put()
{
	_write_lock_put(&wlx);
	wlock_ctx_destroy(&wlx);
	wlock_ctx_disconnect(&wlx);
	m0_free(wlx.wlc_rm_addr);
	M0_LEAVE();
}

static int rm_write_lock_get(struct m0_rpc_machine *rpc_mach, const char *rm_ep)
{
	int                        rc;

	rc = wlock_ctx_create(rpc_mach, rm_ep);
	if (rc != 0)
		goto fail;
	rc = wlock_ctx_connect(&wlx);
	if (rc != 0)
		goto ctx_free;
	wlock_ctx_creditor_setup(&wlx);
	_write_lock_get(&wlx);
	m0_semaphore_down(&wlx.wlc_sem);
	rc = wlx.wlc_rc;
	if (rc != 0)
		goto ctx_destroy;
	return M0_RC(rc);
ctx_destroy:
	wlock_ctx_destroy(&wlx);
	wlock_ctx_disconnect(&wlx);
ctx_free:
	m0_free(wlx.wlc_rm_addr);
fail:
	return M0_ERR(rc);
}

int main(int argc, char **argv)
{
	static struct m0           instance;
	struct m0_net_domain       domain;
	struct m0_net_buffer_pool  buffer_pool;
	struct m0_reqh             reqh;
	struct m0_reqh_service    *rm_service;
	struct m0_fid              process_fid = M0_FID_TINIT('r', 1, 5);
	struct m0_fid              rms_fid = M0_FID_TINIT('s', 3, 10);
	struct m0_rpc_machine      rpc_mach;
	const char                *rm_ep;
	const char                *c_ep;
	int                        delay;
	int                        rc;

	rc = m0_init(&instance);
	if (rc != 0)
		return M0_ERR(rc);
        rc = M0_GETOPTS("m0rwlock", argc, argv,
                            M0_STRINGARG('s',
				         "server endpoint (RM)",
                                       LAMBDA(void, (const char *string) {
                                               rm_ep = string; })),
                            M0_STRINGARG('c',
				         "client endpoint",
                                       LAMBDA(void, (const char *string) {
                                               c_ep = string; })),
                            M0_FORMATARG('d',
				         "delay between write lock get and put",
					 "%i", &delay));
        if (rc != 0)
                return M0_ERR(rc);
	printf("s %s, c %s, d %d\n", rm_ep, c_ep, delay);
	rc = m0_net_domain_init(&domain, &m0_net_lnet_xprt);
	if (rc != 0)
		goto m0_fini;
	rc = m0_rpc_net_buffer_pool_setup(&domain, &buffer_pool, 2, 1);
	if (rc != 0)
		goto domain_fini;
	rc = M0_REQH_INIT(&reqh,
			  .rhia_dtm     = (void *)1,
			  .rhia_mdstore = (void *)1,
			  .rhia_fid     = &process_fid);
	if (rc != 0)
		goto buffer_cleanup;
	m0_reqh_start(&reqh);
	rc = m0_reqh_service_setup(&rm_service, &m0_rms_type, &reqh, NULL,
				   &rms_fid);
	if (rc != 0)
		goto reqh_fini;
	rc = m0_rpc_machine_init(&rpc_mach, &domain, c_ep, &reqh, &buffer_pool,
				 ~(uint32_t)0, 1 << 17, 2);
	if (rc != 0)
		goto services_terminate;
	rc = rm_write_lock_get(&rpc_mach, rm_ep);
	if (rc != 0)
		goto mach_fini;
	sleep(delay);
	rm_write_lock_put();
mach_fini:
        m0_rpc_machine_fini(&rpc_mach);
services_terminate:
        m0_reqh_services_terminate(&reqh);
reqh_fini:
        m0_reqh_fini(&reqh);
buffer_cleanup:
        m0_rpc_net_buffer_pool_cleanup(&buffer_pool);
domain_fini:
        m0_net_domain_fini(&domain);
m0_fini:
        m0_fini();
	return M0_RC(rc);
}
