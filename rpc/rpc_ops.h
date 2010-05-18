#ifndef __COLIBRI_RPC_RPC_OPS_H__

#define __COLIBRI_RPC_RPC_OPS_H__

/**
 RPC commands supported by rpc library
 */
enum c2_rpc_ops {
	/**
	 Create new session on server
	 */
	C2_SESSION_CREATE = 1,
	/**
	 Destroy session on server
	 */
	C2_SESSION_DESTROY,
	/**
	 send compound request over session
	 */
	C2_SESSION_COMPOUND
};

#endif