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
 */
/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#pragma once

#ifndef __MERO_UTIL_PLOSS_PL_H__
#define __MERO_UTIL_PLOSS_PL_H__

#include <rpc/rpc.h>

#include <pthread.h>

#include "helper.h"

#ifdef __cplusplus
extern "C" {
#endif

enum M0_PL_CONFIG_TYPE {
	PL_PROPABILITY = 1,
	PL_DELAY = 2,
	PL_VERBOSE = 3,
};
typedef enum M0_PL_CONFIG_TYPE M0_PL_CONFIG_TYPE;

typedef M0_PL_CONFIG_TYPE m0_pl_config_type;

struct m0_pl_config {
	m0_pl_config_type op;
	uint32_t value;
};
typedef struct m0_pl_config m0_pl_config;

struct m0_pl_config_reply {
	int res;
	union {
		uint32_t config_value;
	} m0_pl_config_reply_u;
};
typedef struct m0_pl_config_reply m0_pl_config_reply;

struct m0_pl_config_res {
	m0_pl_config_type op;
	m0_pl_config_reply body;
};
typedef struct m0_pl_config_res m0_pl_config_res;

struct m0_pl_ping {
	uint32_t seqno;
};
typedef struct m0_pl_ping m0_pl_ping;

struct m0_pl_ping_res {
	uint32_t seqno;
	uint32_t time;
};
typedef struct m0_pl_ping_res m0_pl_ping_res;

#define PLPROG 0x20000076
#define PLVER 1

#if defined(__STDC__) || defined(__cplusplus)
#define PING 1
extern  enum clnt_stat ping_1(struct m0_pl_ping *, struct m0_pl_ping_res *, CLIENT *);
extern  bool_t ping_1_svc(struct m0_pl_ping *, struct m0_pl_ping_res *, struct svc_req *);
#define SETCONFIG 2
extern  enum clnt_stat setconfig_1(struct m0_pl_config *, struct m0_pl_config_res *, CLIENT *);
extern  bool_t setconfig_1_svc(struct m0_pl_config *, struct m0_pl_config_res *, struct svc_req *);
#define GETCONFIG 3
extern  enum clnt_stat getconfig_1(struct m0_pl_config *, struct m0_pl_config_res *, CLIENT *);
extern  bool_t getconfig_1_svc(struct m0_pl_config *, struct m0_pl_config_res *, struct svc_req *);
extern int plprog_1_freeresult (SVCXPRT *, xdrproc_t, caddr_t);

#else /* K&R C */
#define PING 1
extern  enum clnt_stat ping_1();
extern  bool_t ping_1_svc();
#define SETCONFIG 2
extern  enum clnt_stat setconfig_1();
extern  bool_t setconfig_1_svc();
#define GETCONFIG 3
extern  enum clnt_stat getconfig_1();
extern  bool_t getconfig_1_svc();
extern int plprog_1_freeresult ();
#endif /* K&R C */

/* the xdr functions */

#if defined(__STDC__) || defined(__cplusplus)
extern  bool_t xdr_M0_PL_CONFIG_TYPE (XDR *, M0_PL_CONFIG_TYPE*);
extern  bool_t xdr_m0_pl_config_type (XDR *, m0_pl_config_type*);
extern  bool_t xdr_m0_pl_config (XDR *, m0_pl_config*);
extern  bool_t xdr_m0_pl_config_reply (XDR *, m0_pl_config_reply*);
extern  bool_t xdr_m0_pl_config_res (XDR *, m0_pl_config_res*);
extern  bool_t xdr_m0_pl_ping (XDR *, m0_pl_ping*);
extern  bool_t xdr_m0_pl_ping_res (XDR *, m0_pl_ping_res*);

#else /* K&R C */
extern bool_t xdr_M0_PL_CONFIG_TYPE ();
extern bool_t xdr_m0_pl_config_type ();
extern bool_t xdr_m0_pl_config ();
extern bool_t xdr_m0_pl_config_reply ();
extern bool_t xdr_m0_pl_config_res ();
extern bool_t xdr_m0_pl_ping ();
extern bool_t xdr_m0_pl_ping_res ();

#endif /* K&R C */

#ifdef __cplusplus
}
#endif

#endif /* __MERO_UTIL_PLOSS_PL_H__ */
