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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 04/30/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>			/* printf */
#include <string.h>			/* strlen */

#include "lib/getopts.h"		/* c2_getopts */
#include "lib/errno.h"			/* EINVAL */
#include "lib/memory.h"			/* c2_alloc */
#include "lib/semaphore.h"		/* c2_semaphore_down */

#include "colibri/init.h"

#include "net/test/user_space/common_u.h" /* c2_net_test_u_str_copy */
#include "net/test/node.h"

/**
   @page net-test-fspec-cli-node-user Test node command line pamameters
   @todo write
 */

/**
   @defgroup NetTestUNodeInternals Test Node user-space program
   @ingroup NetTestInternals

   @see @ref net-test

   @{
 */

static bool config_check(struct c2_net_test_node_cfg *cfg)
{
	return cfg->ntnc_addr != NULL && cfg->ntnc_addr_console != NULL;
}

static void config_free(struct c2_net_test_node_cfg *cfg)
{
	c2_net_test_u_str_free(cfg->ntnc_addr);
	c2_net_test_u_str_free(cfg->ntnc_addr_console);
}

static void config_print(struct c2_net_test_node_cfg *cfg)
{
	c2_net_test_u_print_s("cfg->ntnc_addr\t\t= %s\n", cfg->ntnc_addr);
	c2_net_test_u_print_s("cfg->ntnc_addr_console\t= %s\n",
			      cfg->ntnc_addr_console);
	c2_net_test_u_print_time("cfg->ntnc_send_timeout",
				 cfg->ntnc_send_timeout);
}

static int configure(int argc, char *argv[], struct c2_net_test_node_cfg *cfg)
{
	bool list_if = false;

	C2_GETOPTS("ntn", argc, argv,
		C2_STRINGARG('a', "Test node commands endpoint",
		LAMBDA(void, (const char *addr) {
			cfg->ntnc_addr = c2_net_test_u_str_copy(addr);
		})),
		C2_STRINGARG('c', "Test console commands endpoint",
		LAMBDA(void, (const char *addr) {
			cfg->ntnc_addr_console = c2_net_test_u_str_copy(addr);
		})),
		C2_IFLISTARG(&list_if),
		C2_HELPARG('?'),
		);
	if (!list_if)
		config_print(cfg);
	return list_if ? 1 : config_check(cfg) ? 0 : -1;
}

int main(int argc, char *argv[])
{
	int rc;
	struct c2_net_test_node_ctx node;
	struct c2_net_test_node_cfg cfg = {
		.ntnc_addr	   = NULL,
		.ntnc_addr_console = NULL,
		.ntnc_send_timeout = C2_TIME(3, 0),
	};

	PRINT("c2_init()\n");
	rc = c2_init();
	c2_net_test_u_print_error("Colibri initialization failed.", rc);
	if (rc != 0)
		return rc;

	/** @todo add Ctrl+C handler
	   c2_net_test_fini()+c2_net_test_config_fini() */
	/** @todo atexit() */
	rc = configure(argc, argv, &cfg);
	if (rc != 0) {
		if (rc == 1) {
			c2_net_test_u_lnet_info();
			rc = 0;
		} else {
			/** @todo where is the error */
			PRINT("Error in configuration.\n");
			config_free(&cfg);
		}
		goto colibri_fini;
	}

	PRINT("c2_net_test_node_init()\n");
	rc = c2_net_test_node_init(&node, &cfg);
	c2_net_test_u_print_error("Test node initialization failed.", rc);
	if (rc != 0)
		goto cfg_free;

	PRINT("c2_net_test_node_start()\n");
	rc = c2_net_test_node_start(&node);
	c2_net_test_u_print_error("Test node start failed.", rc);
	if (rc != 0)
		goto node_fini;

	PRINT("waiting...\n");
	c2_semaphore_down(&node.ntnc_thread_finished_sem);

	PRINT("c2_net_test_node_stop()\n");
	c2_net_test_node_stop(&node);
node_fini:
	PRINT("c2_net_test_node_fini()\n");
	c2_net_test_node_fini(&node);
cfg_free:
	config_free(&cfg);
colibri_fini:
	PRINT("c2_fini()\n");
	c2_fini();

	return rc;
}

/**
   @} end of NetTestUNodeInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
