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

   @todo Use @ref conf "m0_conf" data structures once they are extended to
   support mero service nodes.

   @subsection MGMT-CONF-DLD-fspec-sub Subroutines and Macros

   The following functions are provided to access node configuration:
   - m0_mgmt_conf_init()
   - m0_mgmt_conf_fini()

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

   The m0_mgmt_conf_init() function parses such a file and populates the
   provided m0_mgmt_conf object.  Additional memory is allocated to store
   various configuration information, including the information for each
   service to be started on the node.  The raw network parameters are used
   to build up the values returned in the m0_mgmt_conf::mnc_m0d_ep and
   m0_mgmt_conf::mnc_client_ep members.

   Note that because @ref libgenders3 "libgenders3" is GPL, the nodeattr
   CLI is used to query the genders file, and its output is parsed.

   The m0_mgmt_conf_fini() function frees memory allocated by
   m0_mgmt_conf_init() and returns the m0_mgmt_conf object to
   a pre-initilized state.

   @subsection MGMT-CONF-DLD-lspec-thread Threading and Concurrency Model
   No threads are created.  No internal synchronization is provided.

   <hr>
   @section MGMT-DLD-ut
   - Tests for m0_mgmt_conf_init() to test parsing various genders files.

 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/string.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MGMT
#include "lib/trace.h"
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
   @addtogroup mgmt
   @{
 */

M0_TL_DESCR_DEFINE(m0_mgmt_conf, "mgmt conf svcs", M0_INTERNAL,
		   struct m0_mgmt_svc_conf, msc_link, msc_magic,
		   M0_MGMT_SVC_CONF_MAGIC, M0_MGMT_NODE_CONF_MAGIC);
M0_TL_DEFINE(m0_mgmt_conf, M0_INTERNAL, struct m0_mgmt_svc_conf);

#define NODEATTR_CMD_FMT           "/usr/bin/nodeattr -f %s -l %s"
#define MGMT_CONF_DEFAULT_GENDERS  "/etc/mero/genders"

/** Stores raw information read for a node (without services) */
struct mgmt_conf_node {
	char        *mcn_lnet_if;
	char        *mcn_lnet_pid;
	char        *mcn_lnet_m0d_portal;
	char        *mcn_lnet_client_portal;
	char        *mcn_lnet_host;
	char        *mcn_uuid;
	char        *mcn_var_dir;
	m0_bcount_t  mcn_max_rpc_msg;
	uint32_t     mcn_recv_queue_min_length;
};

enum {
	MGMT_CONF_M0D_TMID = 0,
};

static struct m0_mgmt_svc_conf *
mgmt_svc_conf_find(struct m0_mgmt_conf *conf, char *name)
{
	struct m0_mgmt_svc_conf *svc;

	m0_tl_for(m0_mgmt_conf, &conf->mnc_svc, svc) {
		if (strcmp(svc->msc_name, name) == 0)
			return svc;
	} m0_tlist_endfor;
	return NULL;
}

/**
 * Helper for m0_mgmt_conf_init().  May partially initialize
 * the m0_mgmt_conf on failure.  Caller performs all cleanup.
 */
static int mgmt_svc_add(struct m0_mgmt_conf *conf, char *name, char *args)
{
	struct m0_mgmt_svc_conf *svc = mgmt_svc_conf_find(conf, name);
	char *ptr;
	int i;
	int j;

	if (svc == NULL) {
		M0_ALLOC_PTR(svc);
		if (svc == NULL)
			return -ENOMEM;
		m0_mgmt_conf_tlink_init(svc);
		m0_mgmt_conf_tlist_add_tail(&conf->mnc_svc, svc);
		svc->msc_argc = -1;
		/** @todo use m0_strdup throughout this file when available */
		svc->msc_name = strdup(name);
		if (svc->msc_name == NULL)
			return -ENOMEM;
	}
	if (svc->msc_argc >= 0)		/* no dup services */
		return -EINVAL;
	if (args == NULL || *args == 0) {
		svc->msc_argc = 0;
	} else {
		i = 1;
		for (ptr = args; *ptr != 0; ++ptr) {
			if (*ptr == ':') {
				if (ptr == args || ptr[-1] == ':')
					return -EINVAL;
				++i;
			}
		}
		M0_ALLOC_ARR(svc->msc_argv, i);
		svc->msc_argc = i;
		for (ptr = args, j = 0; j < i; ++ptr) {
			if (*ptr == ':' || *ptr == 0) {
				*ptr = 0;
				svc->msc_argv[j] = strdup(args);
				if (svc->msc_argv[j] == NULL)
					return -EINVAL;
				++j;
				args = ptr + 1;
			}
		}
	}

	return 0;
}

/**
 * Helper for m0_mgmt_conf_init().  May partially initialize
 * the m0_mgmt_conf on failure.  Caller performs all cleanup.
 */
static int mgmt_uuid_add(struct m0_mgmt_conf *conf, char *name, char *uuid)
{
	struct m0_mgmt_svc_conf *svc = mgmt_svc_conf_find(conf, name);

	if (svc == NULL) {
		M0_ALLOC_PTR(svc);
		if (svc == NULL)
			return -ENOMEM;
		m0_mgmt_conf_tlist_add_tail(&conf->mnc_svc, svc);
		svc->msc_argc = -1;
		svc->msc_name = strdup(name);
		if (svc->msc_name == NULL)
			return -ENOMEM;
	}
	if (svc->msc_uuid != NULL)
		return -EINVAL;
	svc->msc_uuid = strdup(uuid);
	if(svc->msc_uuid == NULL)
		return -ENOMEM;

	return 0;
}

static int mgmt_conf_strarg_dup(char **out, char *val)
{
	int rc;

	if (val == NULL) {
		rc = -EINVAL;
	} else {
		*out = strdup(val);
		rc = (*out == NULL) ? -ENOMEM : 0;
	}
	return rc;
}

/** Resolve a hostname to a stringified IP address */
static int mgmt_conf_host_resolve(const char *name, char *buf, size_t bufsiz)
{
	int            i;
	int            rc = 0;
	struct in_addr ipaddr;

	if (inet_aton(name, &ipaddr) == 0) {
		struct hostent  he;
		char            he_buf[4096];
		struct hostent *hp;
		int             herrno;

		rc = gethostbyname_r(name, &he, he_buf, sizeof he_buf,
				     &hp, &herrno);
		if (rc != 0)
			return -ENOENT;
		for (i = 0; hp->h_addr_list[i] != NULL; ++i)
			/* take 1st IPv4 address found */
			if (hp->h_addrtype == AF_INET &&
			    hp->h_length == sizeof(ipaddr))
				break;
		if (hp->h_addr_list[i] == NULL)
			return -EPFNOSUPPORT;
		if (inet_ntop(hp->h_addrtype, hp->h_addr, buf, bufsiz) == NULL)
			rc = -errno;
	} else if (strlen(name) >= bufsiz) {
		rc = -ENOSPC;
	} else {
		strcpy(buf, name);
	}
	return rc;
}

/**
 * Helper for m0_mgmt_conf_init() that populates a mgmt_conf_node with
 * raw information about a node.  If the m0_mgmt_conf object is not NULL,
 * service information is also parsed directly into that object.
 */
static int mgmt_genders_parse(struct mgmt_conf_node *node,
			      struct m0_mgmt_conf   *conf,
			      const char            *genders,
			      const char            *nodename)
{
	struct m0_uint128 uuid;
	char              buf[BUFSIZ];
	char             *cmd;
	char             *ptr;
	char             *val;
	FILE             *p;
	size_t            l;
	int               rc = 0;
	int               rc2;

	M0_SET0(node);
	l = sizeof(NODEATTR_CMD_FMT) + strlen(genders) + strlen(nodename);
	cmd = m0_alloc(l);
	if (cmd == NULL)
		return -ENOMEM;
	sprintf(cmd, NODEATTR_CMD_FMT, genders, nodename);

	errno = 0;
	p = popen(cmd, "r");
	if (p == NULL) {
		rc = (errno == 0) ? -ENOMEM : -errno;
		goto out;
	}

	while (rc == 0) {
		ptr = fgets(buf, BUFSIZ, p);
		if (ptr == NULL)
			break;
		l = strlen(ptr);
		if (l == 0 || ptr[l - 1] != '\n') {
			rc = -EINVAL;
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
			rc = mgmt_conf_strarg_dup(&node->mcn_lnet_if, val);
		} else if (strcmp(ptr, "lnet_pid") == 0) {
			rc = mgmt_conf_strarg_dup(&node->mcn_lnet_pid, val);
		} else if (strcmp(ptr, "lnet_m0d_portal") == 0) {
			rc = mgmt_conf_strarg_dup(&node->mcn_lnet_m0d_portal,
						  val);
		} else if (strcmp(ptr, "lnet_client_portal") == 0) {
			rc = mgmt_conf_strarg_dup(&node->mcn_lnet_client_portal,
						  val);
		} else if (strcmp(ptr, "lnet_host") == 0) {
			rc = mgmt_conf_strarg_dup(&node->mcn_lnet_host, val);
		} else if (strcmp(ptr, "var") == 0) {
			rc = mgmt_conf_strarg_dup(&node->mcn_var_dir, val);
		} else if (strcmp(ptr, "uuid") == 0) {
			if (val != NULL)
				rc = m0_uuid_parse(val, &uuid);
			if (rc == 0)
				rc = mgmt_conf_strarg_dup(&node->mcn_uuid, val);
		} else if (strcmp(ptr, "max_rpc_msg") == 0) {
			if (val != NULL) {
				node->mcn_max_rpc_msg = strtoul(val, &ptr, 0);
				if (*ptr != 0 || node->mcn_max_rpc_msg == 0)
					rc = -EINVAL;
			} else {
				rc = -EINVAL;
			}
		} else if (strcmp(ptr, "min_recv_q") == 0) {
			if (val != NULL) {
				node->mcn_recv_queue_min_length =
				    strtoul(val, &ptr, 0);
				if (*ptr != 0 ||
				    node->mcn_recv_queue_min_length == 0)
					rc = -EINVAL;
			} else {
				rc = -EINVAL;
			}
		} else if (conf != NULL && strncmp(ptr, "s_", 2) == 0) {
			rc = mgmt_svc_add(conf, ptr + 2, val);
		} else if (conf != NULL && strncmp(ptr, "u_", 2) == 0) {
			if (val == NULL)
				rc = -EINVAL;
			else
				rc = m0_uuid_parse(val, &uuid);
			if (rc == 0)
				rc = mgmt_uuid_add(conf, ptr + 2, val);
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

	if (node->mcn_var_dir == NULL ||
	    node->mcn_uuid == NULL ||
	    node->mcn_lnet_if == NULL ||
	    node->mcn_lnet_pid == NULL ||
	    node->mcn_lnet_m0d_portal == NULL ||
	    node->mcn_lnet_host == NULL ||
	    (conf != NULL && m0_mgmt_conf_tlist_is_empty(&conf->mnc_svc)))
		rc = -EINVAL;

out:
	m0_free(cmd);
	return rc;
}

M0_INTERNAL int m0_mgmt_conf_init(struct m0_mgmt_conf *conf,
				  const char          *genders,
				  const char          *nodename)
{
	struct stat              sb;
	struct mgmt_conf_node    node;
	struct m0_mgmt_svc_conf *svc;
	char                     hostname[HOST_NAME_MAX];
	char                     addr[HOST_NAME_MAX];
	char                    *ptr;
	size_t                   l;
	int                      rc;

	M0_PRE(conf != NULL);

	M0_ENTRY();
	if (genders == NULL)
		genders = MGMT_CONF_DEFAULT_GENDERS;
	rc = stat(genders, &sb);
	if (rc < 0)
		M0_RETURN(-errno);
	if (!S_ISREG(sb.st_mode))
		M0_RETURN(-EISDIR);

	rc = gethostname(hostname, sizeof hostname);
	if (rc < 0)
		M0_RETURN(-errno);
	ptr = strchr(hostname, '.');
	if (ptr != NULL)		/* want short name only */
		*ptr = 0;
	if (nodename == NULL)
		nodename = hostname;

	M0_SET0(conf);
	m0_mgmt_conf_tlist_init(&conf->mnc_svc);
	conf->mnc_name = strdup(nodename);
	if (conf->mnc_name == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = mgmt_genders_parse(&node, conf, genders, nodename);
	if (rc != 0)
		goto out;

	m0_tl_for(m0_mgmt_conf, &conf->mnc_svc, svc) {
		if (svc->msc_argc < 0 || svc->msc_uuid == NULL) {
			rc = -EINVAL;
			goto out;
		}
	} m0_tlist_endfor;

	conf->mnc_uuid = node.mcn_uuid;
	node.mcn_uuid = NULL;

	rc = mgmt_conf_host_resolve(node.mcn_lnet_host, addr, sizeof addr);
	if (rc != 0)
		goto out;

	/* 6: 1 "@", 3 colons, 1 character TMID and a nul */
	l = strlen(addr) + strlen(node.mcn_lnet_if) +
	    strlen(node.mcn_lnet_pid) + strlen(node.mcn_lnet_m0d_portal) + 6;
	conf->mnc_m0d_ep = m0_alloc(l);
	if (conf->mnc_m0d_ep == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	sprintf(conf->mnc_m0d_ep, "%s@%s:%s:%s:%d", addr, node.mcn_lnet_if,
		node.mcn_lnet_pid, node.mcn_lnet_m0d_portal,
		MGMT_CONF_M0D_TMID);

	conf->mnc_max_rpc_msg = node.mcn_max_rpc_msg;
	conf->mnc_recv_queue_min_length = node.mcn_recv_queue_min_length;

	if (nodename == hostname || strcmp(nodename, hostname) == 0) {
		if (node.mcn_lnet_client_portal == NULL) {
			rc = -EINVAL;
			goto out;
		}
		conf->mnc_client_uuid = strdup(conf->mnc_uuid);
		if (conf->mnc_client_uuid == NULL) {
			rc = -ENOMEM;
			goto out;
		}
	} else {
		/* second query to get node uuid and client lnet information */
		m0_free(node.mcn_lnet_if);
		m0_free(node.mcn_lnet_pid);
		m0_free(node.mcn_lnet_m0d_portal);
		m0_free(node.mcn_lnet_client_portal);
		m0_free(node.mcn_lnet_host);
		m0_free(node.mcn_uuid);
		m0_free(node.mcn_var_dir);
		rc = mgmt_genders_parse(&node, NULL, genders, hostname);
		if (rc != 0)
			goto out;

		conf->mnc_client_uuid = node.mcn_uuid;
		node.mcn_uuid = NULL;

		rc = mgmt_conf_host_resolve(node.mcn_lnet_host,
					    addr, sizeof addr);
		if (rc != 0)
			goto out;
	}

	rc = stat(node.mcn_var_dir, &sb);
	if (rc < 0) {
		rc = -errno;
		goto out;
	}
	if (!S_ISDIR(sb.st_mode)) {
		rc = -ENOTDIR;
		goto out;
	}
	conf->mnc_var = node.mcn_var_dir;
	node.mcn_var_dir = NULL;

	l = strlen(addr) + strlen(node.mcn_lnet_if) +
	    strlen(node.mcn_lnet_pid) + strlen(node.mcn_lnet_m0d_portal) + 6;
	conf->mnc_client_ep = m0_alloc(l);
	if (conf->mnc_client_ep == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	sprintf(conf->mnc_client_ep, "%s@%s:%s:%s:*", addr, node.mcn_lnet_if,
		node.mcn_lnet_pid, node.mcn_lnet_client_portal);

out:
	if (rc < 0)
		m0_mgmt_conf_fini(conf);
	m0_free(node.mcn_lnet_if);
	m0_free(node.mcn_lnet_pid);
	m0_free(node.mcn_lnet_m0d_portal);
	m0_free(node.mcn_lnet_client_portal);
	m0_free(node.mcn_lnet_host);
	m0_free(node.mcn_uuid);
	m0_free(node.mcn_var_dir);

	M0_RETURN(rc);
}

M0_INTERNAL void m0_mgmt_conf_fini(struct m0_mgmt_conf *conf)
{
	struct m0_mgmt_svc_conf *svc;

	if (conf != NULL) {
		m0_free(conf->mnc_name);
		m0_free(conf->mnc_uuid);
		m0_free(conf->mnc_m0d_ep);
		m0_free(conf->mnc_client_ep);
		m0_free(conf->mnc_client_uuid);
		m0_free(conf->mnc_var);
		m0_tl_for(m0_mgmt_conf, &conf->mnc_svc, svc) {
			m0_mgmt_conf_tlink_del_fini(svc);
			m0_free(svc->msc_name);
			m0_free(svc->msc_uuid);
			while (svc->msc_argc > 0)
				m0_free(svc->msc_argv[--svc->msc_argc]);
			m0_free(svc->msc_argv);
			m0_free(svc);
		} m0_tlist_endfor;
		m0_mgmt_conf_tlist_fini(&conf->mnc_svc);
		M0_SET0(conf);
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
