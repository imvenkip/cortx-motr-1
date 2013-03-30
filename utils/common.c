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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/19/2010
 */

#include <stdio.h>    /* setbuf */
#include <stdlib.h>   /* system */
#include <sys/stat.h> /* mkdir */
#include <unistd.h>   /* chdir */
#include <errno.h>
#include <err.h>      /* warn */
#include <stdbool.h>  /* bool */
#include <string.h>   /* strdup */

#include "yaml.h"
#include "mero/init.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/list.h"
#include "ut/ut.h"
#include "lib/finject.h"
#include "lib/finject_internal.h"
#include "ut/ut.h"

static int reset_sandbox(const char *sandbox)
{
	char *cmd;
	int   rc;

	rc = asprintf(&cmd, "rm -fr '%s'", sandbox);
	M0_ASSERT(rc > 0);

	rc = system(cmd);
	if (rc != 0) {
		/* cleanup might fail for innocent reasons, e.g., unreliable rm
		   on an NFS mount. */
		fprintf(stderr, "sandbox cleanup at \"%s\" failed: %i\n",
			sandbox, rc);
	}

	free(cmd);
	return rc;
}

int unit_start(const char *sandbox)
{
	int result;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	result = m0_init();
	if (result == 0) {
		result = reset_sandbox(sandbox);
		if (result == 0) {
			result = mkdir(sandbox, 0700);
			if (result == 0)
				result = chdir(sandbox);
			if (result != 0)
				result = -errno;
		}
	}
	if (result == 0)
		m0_ut_init();

	return result;
}

void unit_end(const char *sandbox, bool keep_sandbox)
{
	int rc;

	m0_ut_fini();
	m0_fini();

	rc = chdir("..");
	M0_ASSERT(rc == 0);

        if (!keep_sandbox)
                reset_sandbox(sandbox);
}

int parse_test_list(char *str, struct m0_list *list)
{
	char *token;
	char *subtoken;
	char *saveptr = NULL;
	struct m0_test_suite_entry *ts_entry;

	while (true) {
		token = strtok_r(str, ",", &saveptr);
		if (token == NULL)
			break;

		subtoken = strchr(token, ':');
		if (subtoken != NULL)
			*subtoken++ = '\0';

		M0_ALLOC_PTR(ts_entry);
		if (ts_entry == NULL)
			return -ENOMEM;

		ts_entry->tse_suite_name = token;
		/* subtoken can be NULL if no test was specified */
		ts_entry->tse_test_name = subtoken;

		m0_list_link_init(&ts_entry->tse_linkage);
		m0_list_add_tail(list, &ts_entry->tse_linkage);

		/* str should be NULL for subsequent strtok_r(3) calls */
		str = NULL;
	}

	return 0;
}

void free_test_list(struct m0_list *list)
{
	struct m0_test_suite_entry *entry;
	struct m0_test_suite_entry *n;
	m0_list_for_each_entry_safe(list, entry, n,
			struct m0_test_suite_entry, tse_linkage)
	{
		m0_list_del(&entry->tse_linkage);
		m0_free(entry);
	}
}

#ifdef ENABLE_FAULT_INJECTION

int enable_fault_point(char *str)
{
	int  i;
	char *token;
	char *subtoken;
	char *token_saveptr = NULL;
	char *subtoken_saveptr = NULL;

	const char *func;
	const char *tag;
	const char *type;
	const char *data1;
	const char *data2;
	const char **fp_map[] = { &func, &tag, &type, &data1, &data2 };

	struct m0_fi_fpoint_data data = { 0 };

	while (true) {
		func = tag = type = data1 = data2 = NULL;

		token = strtok_r(str, ",", &token_saveptr);
		if (token == NULL)
			break;

		subtoken = token;
		for (i = 0; i < sizeof fp_map; ++i) {
			subtoken = strtok_r(token, ":", &subtoken_saveptr);
			if (subtoken == NULL)
				break;

			*fp_map[i] = subtoken;

			/*
			 * token should be NULL for subsequent strtok_r(3) calls
			 */
			token = NULL;
		}

		if (func == NULL || tag == NULL || type == NULL) {
			fprintf(stderr, "Incorrect fault point specification\n");
			return -EINVAL;
		}

		data.fpd_type = m0_fi_fpoint_type_from_str(type);
		if (data.fpd_type == M0_FI_INVALID_TYPE) {
			fprintf(stderr, "Incorrect fault point type '%s'\n",
					type);
			return -EINVAL;
		}

		if (data.fpd_type == M0_FI_RANDOM) {
			if (data1 == NULL) {
				fprintf(stderr, "No probability was specified"
						" for 'random' FP type\n");
				return -EINVAL;
			}
			data.u.fpd_p = atoi(data1);
		} else if (data.fpd_type == M0_FI_OFF_N_ON_M) {
			if (data1 == NULL || data2 == NULL) {
				fprintf(stderr, "No N or M was specified"
						" for 'off_n_on_m' FP type\n");
				return -EINVAL;
			}
			data.u.s1.fpd_n = atoi(data1);
			data.u.s1.fpd_m = atoi(data2);
		}

		m0_fi_enable_generic(func, tag, &data);

		/* str should be NULL for subsequent strtok_r(3) calls */
		str = NULL;
	}

	return 0;
}

static inline const char *pair_key(yaml_document_t *doc, yaml_node_pair_t *pair)
{
	return (const char*)yaml_document_get_node(doc, pair->key)->data.scalar.value;
}

static inline const char *pair_val(yaml_document_t *doc, yaml_node_pair_t *pair)
{
	return (const char*)yaml_document_get_node(doc, pair->value)->data.scalar.value;
}

static int extract_fpoint_data(yaml_document_t *doc, yaml_node_t *node,
			       const char **func, const char **tag,
			       struct m0_fi_fpoint_data *data)
{
	yaml_node_pair_t *pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; pair++) {
		const char *key = pair_key(doc, pair);
		const char *val = pair_val(doc, pair);
		if (strcmp(key, "func") == 0) {
			*func = val;
		} else if (strcmp(key, "tag") == 0) {
			*tag = val;
		} else if (strcmp(key, "type") == 0) {
			data->fpd_type = m0_fi_fpoint_type_from_str(val);
			if (data->fpd_type == M0_FI_INVALID_TYPE) {
				fprintf(stderr, "Incorrect FP type '%s'\n", val);
				return -EINVAL;
			}
		}
		else if (strcmp(key, "p") == 0) {
			data->u.fpd_p = atoi(val);
		} else if (strcmp(key, "n") == 0) {
			data->u.s1.fpd_n = atoi(val);
		} else if (strcmp(key, "m") == 0) {
			data->u.s1.fpd_m = atoi(val);
		} else {
			fprintf(stderr, "Incorrect key '%s' in yaml file\n", key);
			return -EINVAL;
		}
	}

	return 0;
}

static int process_yaml(yaml_document_t *doc)
{
	int          rc;
	yaml_node_t  *node;
	const char   *func = 0;
	const char   *tag = 0;

	struct m0_fi_fpoint_data data = { 0 };

	for (node = doc->nodes.start; node < doc->nodes.top; node++)
		if (node->type == YAML_MAPPING_NODE) {
			rc = extract_fpoint_data(doc, node, &func, &tag, &data);
			if (rc != 0)
				return rc;
			m0_fi_enable_generic(strdup(func), strdup(tag), &data);
		}

	return 0;
}

int enable_fault_points_from_file(const char *file_name)
{
	int rc = 0;
	FILE *f;
	yaml_parser_t parser;
	yaml_document_t document;

	f = fopen(file_name, "r");
	if (f == NULL) {
		warn("Failed to open fault point yaml file '%s'", file_name);
		return -ENOENT;
	}

	rc = yaml_parser_initialize(&parser);
	if (rc != 1) {
		fprintf(stderr, "Failed to init yaml parser\n");
		rc = -EINVAL;
		goto fclose;
	}

	yaml_parser_set_input_file(&parser, f);

	rc = yaml_parser_load(&parser, &document);
	if (rc != 1) {
		fprintf(stderr, "Incorrect YAML file\n");
		rc = -EINVAL;
		goto pdel;
	}

	rc = process_yaml(&document);

	yaml_document_delete(&document);
pdel:
	yaml_parser_delete(&parser);
fclose:
	fclose(f);

	return rc;
}

#else /* ENABLE_FAULT_INJECTION */

int enable_fault_point(char *str)
{
	return 0;
}

int enable_fault_points_from_file(const char *file_name)
{
	return 0;
}

#endif /* ENABLE_FAULT_INJECTION */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
