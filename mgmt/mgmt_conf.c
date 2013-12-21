/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 13-Mar-2013
 */

/**
   @page MGMT-CONF-DLD Management Configuration Internals

   This is a component DLD and hence not all sections are present.
   Refer to the @ref MGMT-DLD "Management Interface Design"
   for the design requirements.

   - @ref MGMT-CONF-DLD-ovw
   - @ref MGMT-CONF-DLD-fspec
     - @ref MGMT-CONF-DLD-fspec-ds
     - @ref MGMT-CONF-DLD-fspec-sub
   - @ref MGMT-CONF-DLD-lspec

   <hr>
   @section MGMT-CONF-DLD-ovw Overview
   - An API and data structures to query for the configuration
   of a Mero service node.
   - Used by both m0ctl and m0d.

   <hr>
   @section MGMT-CONF-DLD-fspec Functional Specification
   - @ref MGMT-CONF-DLD-fspec-ds
   - @ref MGMT-CONF-DLD-fspec-sub

   @subsection MGMT-CONF-DLD-fspec-ds Data Structures

   The following data structures are provided:
   - m0_mgmt_conf
   - m0_mgmt_svc_conf
   - m0_mgmt_node_conf
   - m0_mgmt_client_conf
   - m0_mgmt_service_ep_conf

   @todo Use @ref conf "m0_conf" data structures once they are extended to
   support mero service nodes.

   @subsection MGMT-CONF-DLD-fspec-sub Subroutines and Macros

   The following functions are provided to access node configuration:
   - m0_mgmt_conf_init()
   - m0_mgmt_conf_fini()
   - m0_mgmt_node_get()
   - m0_mgmt_node_free()
   - m0_mgmt_client_get()
   - m0_mgmt_client_free()
   - m0_mgmt_service_ep_get()
   - m0_mgmt_service_ep_free()

   <hr>
   @section MGMT-CONF-DLD-lspec Logical Specification
   - @ref MGMT-CONF-DLD-lspec-comps
   - @ref MGMT-CONF-DLD-lspec-thread

   @subsection MGMT-CONF-DLD-lspec-comps Component Overview
   The management configuration functions provide a mechanism to determine
   the configuration parameters applicable to managing a mero node.

   At present, only server nodes are addressed.

   A genders file, as described in @ref MGMT-DLD-lspec-genders
   is the basis of management configuration.

   The m0_mgmt_conf_init() function initializes a m0_mgmt_conf object.  It
   verifies the existence of the specified genders file.  It uses an instance of
   m0_mgmt_conf_private to initialize the m0_mgmt_conf::mc_private and caches
   the genders file name and local name name in the private data.

   The m0_mgmt_conf_fini() function frees memory allocated by
   m0_mgmt_conf_init() and returns the m0_mgmt_conf object to a pre-initialized
   state.

   The m0_mgmt_node_get() function queries the genders file for server node
   information and populates the provided m0_mgmt_node_conf object.  Additional
   memory is allocated to store various configuration information, including the
   information for each service to be started on the node.  The raw network
   parameters are used to build up the value returned in the
   m0_mgmt_node_conf::mnc_m0d_ep member.

   The m0_mgmt_node_free() function frees memory allocated by m0_mgmt_node_get()
   and returns the m0_mgmt_node_conf object to a pre-initialized state.

   The m0_mgmt_client_get() function queries the genders file for information
   reqired to run a management client and populates the provided
   m0_mgmt_client_conf object.  Additional memory is allocated to store various
   configuration information.  The raw network parameters are used to build up
   the value returned in the m0_mgmt_client_conf::mcc_mgmt_ep member.

   The m0_mgmt_client_free() function frees memory allocated by
   m0_mgmt_client_get() and returns the m0_mgmt_client_conf object to a
   pre-initialized state.

   The m0_mgmt_service_ep_get() function queries the genders file for
   information about all configured endpoints for a specific service type and
   populates the provided m0_mgmt_service_ep_conf object.  Additional memory is
   allocated to store various configuration information.  The raw network
   parameters are used to build up the values returned in the
   m0_mgmt_service_ep_conf::mcc_ep array.  The order of the endpoints returned
   in the array is arbitrary.  This function does not attempt to determine which
   service instances are alive or active, only their configured endpoints.

   The m0_mgmt_service_ep_free() function frees memory allocated by
   m0_mgmt_service_ep_get() and returns the m0_mgmt_service_ep_conf object to
   a pre-initialized state.

   Note that because @ref libgenders3 "libgenders3" is GPL, the nodeattr
   CLI is used to query the genders file, and its output is parsed.

   @subsection MGMT-CONF-DLD-lspec-thread Threading and Concurrency Model
   No threads are created.  No internal synchronization is provided.

   <hr>
   @section MGMT-DLD-ut
   - Tests for m0_mgmt_conf_init() to test parsing various genders files.

 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MGMT
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/string.h" /* m0_strdup */
#include "lib/types.h"
#include "lib/uuid.h"
#include "mero/magic.h"
#include "mgmt/mgmt.h"

#include <arpa/inet.h> /* inet_ntoa, inet_ntop */
#include <sys/stat.h>
#include <netdb.h>  /* gethostbyname_r */
#include <limits.h> /* HOST_NAME_MAX */
#include <unistd.h> /* gethostname */

/**
   @addtogroup mgmt_pvt
   @{
 */

struct m0_mgmt_conf_private {
	char *mcp_genders;
	/** node name of local host (i.e. short hostname) */
	char *mcp_nodename;
};

struct m0_mgmt_conf_names {
	int    mcn_nr;
	char **mcn_name;
};

/** @} end of mgmt_pvt group */

/**
   @addtogroup mgmt
   @{
 */

M0_TL_DESCR_DEFINE(m0_mgmt_conf, "mgmt conf svcs", M0_INTERNAL,
		   struct m0_mgmt_svc_conf, msc_link, msc_magic,
		   M0_MGMT_SVC_CONF_MAGIC, M0_MGMT_NODE_CONF_MAGIC);
M0_TL_DEFINE(m0_mgmt_conf, M0_INTERNAL, struct m0_mgmt_svc_conf);

#define NODEATTR_NODE_CMD_FMT      "/usr/bin/nodeattr -f %s -l %s"
#define NODEATTR_SVC_CMD_FMT       "/usr/bin/nodeattr -f %s -s m0_s_%s"
#define MGMT_CONF_DEFAULT_GENDERS  "/etc/mero/genders"

enum {
	MGMT_CONF_M0D_TMID = 1,
};

static struct m0_mgmt_svc_conf *
mgmt_svc_conf_find(struct m0_mgmt_node_conf *node, char *name)
{
	struct m0_mgmt_svc_conf *svc;

	m0_tl_for(m0_mgmt_conf, &node->mnc_svc, svc) {
		if (strcmp(svc->msc_name, name) == 0)
			return svc;
	} m0_tlist_endfor;
	return NULL;
}

/**
 * Parse a string of tokens separated by a specific character into an allocated
 * argc/argv style array.
 */
static int mgmt_strarg_parse(int *out_nr, char ***out, char *in, char sep)
{
	char *ptr;
	int   i = 1;
	int   j;

	for (ptr = in; *ptr != 0; ++ptr) {
		if (*ptr == sep) {
			if (ptr == in || ptr[-1] == sep)
				M0_RETERR(-EINVAL, "consecutive separators");
			++i;
		}
	}
	M0_ALLOC_ARR(*out, i);
	if (*out == NULL)
		return -ENOMEM;
	*out_nr = i;
	for (ptr = in, j = 0; j < i; ++ptr) {
		if (*ptr == sep || *ptr == 0) {
			*ptr = 0;
			(*out)[j] = m0_strdup(in);
			if ((*out)[j] == NULL)
				return -ENOMEM;
			++j;
			in = ptr + 1;
		}
	}
	return 0;
}

/**
 * Add a service and/or its UUID to a node. Should be called twice for each
 * service, once for the m0_s_ attribute and once for the corresponding m0_u_
 * attribute. May partially initialize the m0_mgmt_node_conf on failure. Caller
 * performs all cleanup.
 * @param node Service is added to this node
 * @param name Name of the service
 * @param args Service arguments, may be NULL (i.e. for an m0_u_ attribute).
 * @param uuid Service UUID, may be NULL (i.e. for an m0_s_ attribute).
 */
static int mgmt_svc_add(struct m0_mgmt_node_conf *node, char *name, char *args,
			char *uuid)
{
	struct m0_mgmt_svc_conf *svc = mgmt_svc_conf_find(node, name);
	int                      rc = 0;

	if (svc == NULL) {
		M0_ALLOC_PTR(svc);
		if (svc == NULL)
			return -ENOMEM;
		m0_mgmt_conf_tlink_init(svc);
		m0_mgmt_conf_tlist_add_tail(&node->mnc_svc, svc);
		svc->msc_argc = -1;
		svc->msc_name = m0_strdup(name);
		if (svc->msc_name == NULL)
			return -ENOMEM;
	}
	if (uuid != NULL) {
		if (svc->msc_uuid == NULL) {
			svc->msc_uuid = m0_strdup(uuid);
			if(svc->msc_uuid == NULL)
				rc = -ENOMEM;
		}
	} else if (svc->msc_argc < 0) {
		if (args == NULL || *args == 0)
			svc->msc_argc = 0;
		else
			rc = mgmt_strarg_parse(&svc->msc_argc, &svc->msc_argv,
					       args, ';');
	}

	return rc;
}

static int mgmt_conf_strarg_dup(char **out, char *val)
{
	if (val == NULL)
		M0_RETERR(-EINVAL, "missing required value");
	*out = m0_strdup(val);
	return (*out == NULL) ? -ENOMEM : 0;
}

/**
 * Populate a m0_mgmt_node_conf and/or m0_mgmt_client_conf with information
 * about a node.  May partially initialize the object(s) on failure.  Caller
 * performs all cleanup.
 */
static int mgmt_node_query(struct m0_mgmt_node_conf   *node,
			   struct m0_mgmt_client_conf *clnt,
			   const char                 *genders,
			   const char                 *nodename)
{
	struct m0_uint128        uuid;
	struct m0_mgmt_svc_conf *svc;
	char                    *lnet_if = NULL;
	char                    *lnet_pid = NULL;
	char                    *lnet_m0d_portal = NULL;
	char                    *lnet_client_portal = NULL;
	char                    *lnet_host = NULL;
	char                     buf[BUFSIZ];
	char                     addr[HOST_NAME_MAX];
	char                    *cmd;
	char                    *ptr;
	char                    *val;
	FILE                    *p;
	size_t                   l;
	int                      rc = 0;
	int                      rc2;

	M0_PRE(node != NULL || clnt != NULL);
	M0_PRE(genders != NULL && nodename != NULL);
	l = sizeof(NODEATTR_NODE_CMD_FMT) + strlen(genders) + strlen(nodename);
	cmd = m0_alloc(l);
	if (cmd == NULL)
		return -ENOMEM;
	sprintf(cmd, NODEATTR_NODE_CMD_FMT, genders, nodename);

	errno = 0;
	p = popen(cmd, "r");
	if (p == NULL) {
		rc = (errno == 0) ? -ENOMEM : -errno;
		M0_LOG(M0_ERROR, "< rc=%d", rc);
		goto out;
	}

	while (rc == 0) {
		ptr = fgets(buf, BUFSIZ, p);
		if (ptr == NULL)
			break;
		l = strlen(ptr);
		if (l == 0 || ptr[l - 1] != '\n') {
			rc = -EINVAL;
			M0_LOG(M0_ERROR, "< rc=%d", rc);
			break;
		}
		ptr[l - 1] = 0;
		if (strncmp(ptr, "m0_", 3) != 0)
			continue;
		ptr += 3;
		val = strchr(ptr, '=');
		if (val != NULL) {
			*val++ = 0;
			if (*val == 0)
				val = NULL;
		}

		if (strcmp(ptr, "lnet_if") == 0) {
			rc = mgmt_conf_strarg_dup(&lnet_if, val);
		} else if (strcmp(ptr, "lnet_pid") == 0) {
			rc = mgmt_conf_strarg_dup(&lnet_pid, val);
		} else if (strcmp(ptr, "lnet_m0d_portal") == 0) {
			if (node != NULL)
				rc = mgmt_conf_strarg_dup(&lnet_m0d_portal,
							  val);
		} else if (strcmp(ptr, "lnet_client_portal") == 0) {
			if (clnt != NULL)
				rc = mgmt_conf_strarg_dup(&lnet_client_portal,
							  val);
		} else if (strcmp(ptr, "lnet_host") == 0) {
			rc = mgmt_conf_strarg_dup(&lnet_host, val);
		} else if (strcmp(ptr, "datadir") == 0) {
			if (node != NULL)
				rc = mgmt_conf_strarg_dup(&node->mnc_var, val);
		} else if (strcmp(ptr, "uuid") == 0) {
			if (val != NULL) {
				rc = m0_uuid_parse(val, &uuid);
				if (rc != 0)
					M0_LOG(M0_ERROR,
					       "< rc=%d invalid node uuid %s",
					       rc, val);
			}
			if (rc == 0 && node != NULL)
				rc = mgmt_conf_strarg_dup(&node->mnc_uuid, val);
			if (rc == 0 && clnt != NULL)
				rc = mgmt_conf_strarg_dup(&clnt->mcc_uuid, val);
		} else if (strcmp(ptr, "max_rpc_msg") == 0) {
			m0_bcount_t t;

			if (val != NULL) {
				t = strtoul(val, &ptr, 0);
				if (*ptr != 0 || t == 0) {
					rc = -EINVAL;
					M0_LOG(M0_ERROR, "< rc=%d", rc);
				} else {
					if (node != NULL)
						node->mnc_max_rpc_msg = t;
					if (clnt != NULL)
						clnt->mcc_max_rpc_msg = t;
				}
			} else {
				rc = -EINVAL;
				M0_LOG(M0_ERROR, "< rc=%d", rc);
			}
		} else if (strcmp(ptr, "min_recv_q") == 0) {
			uint32_t t;

			if (val != NULL) {
				t = strtoul(val, &ptr, 0);
				if (*ptr != 0 || t == 0) {
					rc = -EINVAL;
					M0_LOG(M0_ERROR, "< rc=%d", rc);
				} else {
					if (node != NULL)
						node->mnc_recvq_min_len = t;
					if (clnt != NULL)
						clnt->mcc_recvq_min_len = t;
				}
			} else {
				rc = -EINVAL;
				M0_LOG(M0_ERROR, "< rc=%d", rc);
			}
		} else if (node != NULL && strncmp(ptr, "s_", 2) == 0) {
			rc = mgmt_svc_add(node, ptr + 2, val, NULL);
		} else if (node != NULL && strncmp(ptr, "u_", 2) == 0) {
			if (val == NULL)
				rc = -EINVAL;
			else
				rc = m0_uuid_parse(val, &uuid);
			if (rc == 0)
				rc = mgmt_svc_add(node, ptr + 2, NULL, val);
			if (rc != 0)
				M0_LOG(M0_ERROR,
				       "< rc=%d invalid or missing uuid: %s",
				       rc, ptr);
		}
	}

	rc2 = pclose(p);
	if (rc == 0) {
		rc = rc2;
		if (rc < 0)
			rc = -errno;
		else if (rc > 0)
			rc = -EINVAL;
	}

	if (lnet_if == NULL || lnet_pid == NULL || lnet_host == NULL) {
		rc = -EINVAL;
		M0_LOG(M0_ERROR, "< rc=%d %s missing lnet if, pid and/or host",
		       rc, nodename);
		goto out;
	}

	if (node != NULL) {
		if (node->mnc_var == NULL ||
		    node->mnc_uuid == NULL ||
		    lnet_m0d_portal == NULL ||
		    m0_mgmt_conf_tlist_is_empty(&node->mnc_svc)) {
			rc = -EINVAL;
			M0_LOG(M0_ERROR,
			       "< rc=%d %s missing var uuid portal and/or svcs",
			       rc, nodename);
			goto out;
		}
		m0_tl_for(m0_mgmt_conf, &node->mnc_svc, svc) {
			if (svc->msc_argc < 0 || svc->msc_uuid == NULL) {
				rc = -EINVAL;
				M0_LOG(M0_ERROR,
				       "< rc=%d %s missing service or uuid",
				       rc, svc->msc_name);
				goto out;
			}
		} m0_tlist_endfor;
	}
	if (clnt != NULL &&
	    (clnt->mcc_uuid == NULL || lnet_client_portal == NULL)) {
		rc = -EINVAL;
		M0_LOG(M0_ERROR, "< rc=%d %s missing uuid or portal",
		       rc, nodename);
		goto out;
	}

	rc = m0_host_resolve(lnet_host, addr, sizeof addr);
	if (rc != 0)
		goto out;

	/* 6: 1 "@", 3 colons, 1 character TMID and a nul */
	if (node != NULL) {
		l = strlen(addr) + strlen(lnet_if) +
		    strlen(lnet_pid) + strlen(lnet_m0d_portal) + 6;
		node->mnc_m0d_ep = m0_alloc(l);
		if (node->mnc_m0d_ep == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		sprintf(node->mnc_m0d_ep, "%s@%s:%s:%s:%d", addr, lnet_if,
			lnet_pid, lnet_m0d_portal, MGMT_CONF_M0D_TMID);
	}
	if (clnt != NULL) {
		l = strlen(addr) + strlen(lnet_if) +
		    strlen(lnet_pid) + strlen(lnet_client_portal) + 6;
		clnt->mcc_mgmt_ep = m0_alloc(l);
		if (clnt->mcc_mgmt_ep == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		sprintf(clnt->mcc_mgmt_ep, "%s@%s:%s:%s:*", addr, lnet_if,
			lnet_pid, lnet_client_portal);
	}

out:
	m0_free(cmd);
	m0_free(lnet_if);
	m0_free(lnet_pid);
	m0_free(lnet_m0d_portal);
	m0_free(lnet_client_portal);
	m0_free(lnet_host);
	return rc;
}

/**
 * Query genders for the name(s) of the node(s) running the specified service.
 * @return On success, names is populated with the names of the matching
 * nodes.  On failure, names may be partially populated.  Caller frees all
 * allocated memory.
 */
static int mgmt_svc_names_query(struct m0_mgmt_conf_names *names,
				const char                *genders,
				const char                *svc_name)
{
	char   buf[BUFSIZ];
	char  *cmd;
	char  *ptr;
	FILE  *p;
	size_t l;
	int    rc;

	M0_PRE(names != NULL && genders != NULL && svc_name != NULL);
	M0_SET0(names);
	l = sizeof(NODEATTR_SVC_CMD_FMT) + strlen(genders) + strlen(svc_name);
	cmd = m0_alloc(l);
	if (cmd == NULL)
		return -ENOMEM;
	sprintf(cmd, NODEATTR_SVC_CMD_FMT, genders, svc_name);

	errno = 0;
	p = popen(cmd, "r");
	if (p == NULL) {
		rc = (errno == 0) ? -ENOMEM : -errno;
		m0_free(cmd);
		M0_RETURN(rc);
	}
	m0_free(cmd);

	/* assume one line of output */
	ptr = fgets(buf, BUFSIZ, p);
	rc = pclose(p);
	if (rc < 0)
		M0_RETURN(-errno);
	else if (rc > 0)
		M0_RETURN(-EINVAL);
	if (ptr == NULL)
		M0_RETURN(-ENOENT);
	l = strlen(ptr);
	if (l == 0 || ptr[l - 1] != '\n')
		M0_RETURN(-EINVAL);
	ptr[l - 1] = 0;

	return mgmt_strarg_parse(&names->mcn_nr, &names->mcn_name, buf, ' ');
}

M0_INTERNAL int m0_mgmt_node_get(struct m0_mgmt_conf      *conf,
				 const char               *nodename,
				 struct m0_mgmt_node_conf *node)
{
	int rc;

	M0_PRE(conf != NULL && conf->mc_private != NULL && node != NULL);

	if (nodename == NULL)
		nodename = conf->mc_private->mcp_nodename;

	M0_SET0(node);
	m0_mgmt_conf_tlist_init(&node->mnc_svc);
	rc = mgmt_node_query(node, NULL,
			     conf->mc_private->mcp_genders, nodename);
	if (rc < 0)
		m0_mgmt_node_free(node);
	M0_RETURN(rc);
}

M0_INTERNAL void m0_mgmt_node_free(struct m0_mgmt_node_conf *node)
{
	struct m0_mgmt_svc_conf *svc;

	if (node != NULL) {
		m0_free(node->mnc_uuid);
		m0_free(node->mnc_m0d_ep);
		m0_free(node->mnc_var);
		m0_tl_for(m0_mgmt_conf, &node->mnc_svc, svc) {
			m0_mgmt_conf_tlink_del_fini(svc);
			m0_free(svc->msc_name);
			m0_free(svc->msc_uuid);
			while (svc->msc_argc > 0)
				m0_free(svc->msc_argv[--svc->msc_argc]);
			m0_free(svc->msc_argv);
			m0_free(svc);
		} m0_tlist_endfor;
		m0_mgmt_conf_tlist_fini(&node->mnc_svc);
		M0_SET0(node);
	}
}

M0_INTERNAL int m0_mgmt_client_get(struct m0_mgmt_conf        *conf,
				   struct m0_mgmt_client_conf *clnt)
{
	int rc;

	M0_PRE(conf != NULL && conf->mc_private != NULL && clnt != NULL);

	M0_SET0(clnt);
	rc = mgmt_node_query(NULL, clnt, conf->mc_private->mcp_genders,
			     conf->mc_private->mcp_nodename);
	if (rc < 0)
		m0_mgmt_client_free(clnt);
	M0_RETURN(rc);
}

M0_INTERNAL void m0_mgmt_client_free(struct m0_mgmt_client_conf *clnt)
{
	if (clnt != NULL) {
		m0_free(clnt->mcc_mgmt_ep);
		m0_free(clnt->mcc_uuid);
		M0_SET0(clnt);
	}
}

M0_INTERNAL int m0_mgmt_service_ep_get(struct m0_mgmt_conf            *conf,
				       const char                     *service,
				       struct m0_mgmt_service_ep_conf *svc)
{
	struct m0_mgmt_conf_names names;
	struct m0_mgmt_node_conf  node;
	int                       i;
	int                       rc;

	M0_PRE(conf != NULL && service != NULL && svc != NULL);

	M0_SET0(svc);
	rc = mgmt_svc_names_query(&names, conf->mc_private->mcp_genders,
				  service);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "< rc=%d", rc);
		goto out;
	}
	M0_ASSERT(names.mcn_nr > 0);

	M0_ALLOC_ARR(svc->mse_ep, names.mcn_nr);
	if (svc->mse_ep == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	svc->mse_ep_nr = names.mcn_nr;

	for (i = 0; i < names.mcn_nr; ++i) {
		rc = m0_mgmt_node_get(conf, names.mcn_name[i], &node);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "< rc=%d", rc);
			goto out;
		}
		svc->mse_ep[i] = node.mnc_m0d_ep;
		node.mnc_m0d_ep = NULL;
		m0_mgmt_node_free(&node);
	}

out:
	for (i = names.mcn_nr; i > 0; )
		m0_free(names.mcn_name[--i]);
	m0_free(names.mcn_name);
	if (rc != 0)
		m0_mgmt_service_ep_free(svc);
	return rc;
}

M0_INTERNAL void m0_mgmt_service_ep_free(struct m0_mgmt_service_ep_conf *svc)
{
	int i;

	if (svc != NULL) {
		for (i = svc->mse_ep_nr; i > 0; )
			m0_free(svc->mse_ep[--i]);
		m0_free(svc->mse_ep);
		M0_SET0(svc);
	}
}


M0_INTERNAL int m0_mgmt_conf_init(struct m0_mgmt_conf *conf,
				  const char          *genders)
{
	int         rc;
	struct stat sb;
	char        hostname[HOST_NAME_MAX];
	char       *ptr;

	M0_PRE(conf != NULL);

	M0_ENTRY();
	M0_SET0(conf);
	if (genders == NULL)
		genders = MGMT_CONF_DEFAULT_GENDERS;
	rc = stat(genders, &sb);
	if (rc < 0)
		M0_RETERR(-errno, "%s", genders);
	if (!S_ISREG(sb.st_mode))
		M0_RETERR(-EISDIR, "%s", genders);

	rc = gethostname(hostname, sizeof hostname);
	if (rc < 0)
		M0_RETURN(-errno);
	ptr = strchr(hostname, '.');
	if (ptr != NULL)		/* want short name only */
		*ptr = 0;

	M0_ALLOC_PTR(conf->mc_private);
	if (conf->mc_private == NULL)
		M0_RETURN(-ENOMEM);
	conf->mc_private->mcp_genders = m0_strdup(genders);
	conf->mc_private->mcp_nodename = m0_strdup(hostname);
	if (conf->mc_private->mcp_genders == NULL ||
	    conf->mc_private->mcp_nodename == NULL) {
		m0_mgmt_conf_fini(conf);
		M0_RETURN(-ENOMEM);
	}

	M0_RETURN(rc);
}

M0_INTERNAL void m0_mgmt_conf_fini(struct m0_mgmt_conf *conf)
{
	if (conf != NULL && conf->mc_private != NULL) {
		m0_free(conf->mc_private->mcp_genders);
		m0_free(conf->mc_private->mcp_nodename);
		m0_free(conf->mc_private);
		conf->mc_private = NULL;
	}
}

/** @} end of mgmt group */

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
