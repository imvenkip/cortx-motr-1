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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 22/03/2011
 */

#include "lib/errno.h"
#include "lib/ut.h"
#include "lib/cdefs.h"
#include "lib/memory.h"

#include "console/console_yaml.h"
#include "yaml2db/yaml2db.h"

/**
   @addtogroup console_yaml
   @{
*/

static struct c2_cons_yaml_info yaml_info;

C2_INTERNAL int c2_cons_yaml_init(const char *file_path)
{
	int          rc;
	yaml_node_t *root_node;

	C2_PRE(file_path != NULL);

	yaml_info.cyi_file = fopen(file_path, "r");

	if(yaml_info.cyi_file == NULL) {
		perror("Failed to open file ");
		printf("%s, errno = %d\n", file_path, errno);
		goto error;
	}
	/* Initialize parser */
	rc = yaml_parser_initialize(&yaml_info.cyi_parser);
	if(rc != 1) {
		fprintf(stderr, "Failed to initialize parser!\n");
		fclose(yaml_info.cyi_file);
		goto error;
	}

	/* Set input file */
	yaml_parser_set_input_file(&yaml_info.cyi_parser, yaml_info.cyi_file);

	/* Load document */
	rc = yaml_parser_load(&yaml_info.cyi_parser, &yaml_info.cyi_document);
        if (rc != 1) {
		yaml_parser_delete(&yaml_info.cyi_parser);
		fclose(yaml_info.cyi_file);
		fprintf(stderr, "yaml parser load failed!!\n");
		goto error;
	}

	root_node = yaml_document_get_root_node(&yaml_info.cyi_document);
        if (root_node == NULL) {
                yaml_document_delete(&yaml_info.cyi_document);
		yaml_parser_delete(&yaml_info.cyi_parser);
		fclose(yaml_info.cyi_file);
                fprintf(stderr, "document get root node failed\n");
		goto error;
        }

	yaml_info.cyi_current = yaml_info.cyi_document.nodes.start;
	yaml_support = true;

	return 0;
error:
	c2_yaml_parser_error_detect(&yaml_info.cyi_parser);
	return -EINVAL;
}

C2_INTERNAL void c2_cons_yaml_fini(void)
{
	yaml_support = false;
	if (&yaml_info.cyi_document != NULL)
		yaml_document_delete(&yaml_info.cyi_document);
	if (&yaml_info.cyi_parser != NULL)
		yaml_parser_delete(&yaml_info.cyi_parser);
	fclose(yaml_info.cyi_file);
}

static yaml_node_t *search_node(const char *name)
{
	yaml_document_t *doc   = &yaml_info.cyi_document;
	yaml_node_t     *node  = yaml_info.cyi_current;
        bool             found = false;
	unsigned char   *data_value;

        for ( ; node < doc->nodes.top; node++) {
                if(node->type == YAML_SCALAR_NODE) {
			data_value = node->data.scalar.value;
                        if (strcmp((const char *)data_value, name) == 0){
				node++;
				found = true;
                        }
                }
		if (found) {
			yaml_info.cyi_current = node;
			break;
		}
        }

	if (!found)
		node = NULL;

	return node;
}

C2_INTERNAL void *c2_cons_yaml_get_value(const char *name)
{
	yaml_node_t *node;

	node = search_node(name);
	if (node == NULL)
		return NULL;

	return node->data.scalar.value;
}

C2_INTERNAL int c2_cons_yaml_set_value(const char *name, void *data)
{
	int rc = -ENOTSUP;

	return rc;
}

/** @} end of console_yaml group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

