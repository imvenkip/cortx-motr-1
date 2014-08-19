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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/09/2010
 * Modified by: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Modification date: 25-Mar-2013
 */

#include <stdlib.h>                /* system */
#include <stdio.h>                 /* asprintf,setbuf */
#include <unistd.h>                /* dup, dup2 */
#include <sys/stat.h>              /* mkdir */
#include <err.h>                   /* warn */
#include <yaml.h>                  /* yaml_parser_t */

#include "lib/memory.h"           /* M0_ALLOC_PTR */
#include "lib/errno.h"            /* EINVAL */
#include "lib/string.h"           /* m0_strdup */
#include "lib/list.h"             /* m0_list */
#include "lib/finject.h"          /* m0_fi_fpoint_data */
#include "lib/finject_internal.h" /* m0_fi_fpoint_type_from_str */
#include "ut/ut_internal.h"
#include "ut/ut.h"


static int remove_sandbox(const char *sandbox)
{
	char *cmd;
	int   rc;

	if (sandbox == NULL)
		return 0;

	rc = asprintf(&cmd, "rm -fr '%s'", sandbox);
	M0_ASSERT(rc > 0);

	rc = system(cmd);
	if (rc != 0)
		warn("*WARNING* sandbox cleanup at \"%s\" failed: %i\n",
		     sandbox, rc);

	free(cmd);
	return rc;
}

int m0_arch_ut_init(const struct m0_ut_cfg *config)
{
	int rc;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (config->uc_sandbox == NULL)
		return 0;

	rc = remove_sandbox(config->uc_sandbox);
	if (rc != 0)
		return rc;

	rc = mkdir(config->uc_sandbox, 0700) ?: chdir(config->uc_sandbox);
	if (rc != 0)
		/* don't care about return value of remove_sandbox() here */
		remove_sandbox(config->uc_sandbox);

	return rc;
}

void m0_arch_ut_fini(const struct m0_ut_cfg *config)
{
	int rc;

	rc = chdir("..");
	M0_ASSERT(rc == 0);

        if (!config->uc_keep_sandbox)
                remove_sandbox(config->uc_sandbox);
}

M0_INTERNAL void m0_stream_redirect(FILE * stream, const char *path,
				    struct m0_ut_redirect *redir)
{
	FILE *result;

	/*
	 * This solution is based on the method described in the comp.lang.c
	 * FAQ list, Question 12.34: "Once I've used freopen, how can I get the
	 * original stdout (or stdin) back?"
	 *
	 * http://c-faq.com/stdio/undofreopen.html
	 * http://c-faq.com/stdio/rd.kirby.c
	 *
	 * It's not portable and will only work on systems which support dup(2)
	 * and dup2(2) system calls (these are supported in Linux).
	 */
	redir->ur_stream = stream;
	fflush(stream);
	fgetpos(stream, &redir->ur_pos);
	redir->ur_oldfd = fileno(stream);
	redir->ur_fd = dup(redir->ur_oldfd);
	M0_ASSERT(redir->ur_fd != -1);
	result = freopen(path, "a+", stream);
	M0_ASSERT(result != NULL);
}

M0_INTERNAL void m0_stream_restore(const struct m0_ut_redirect *redir)
{
	int result;

	/*
	 * see comment in m0_stream_redirect() for detailed information
	 * about how to redirect and restore standard streams
	 */
	fflush(redir->ur_stream);
	result = dup2(redir->ur_fd, redir->ur_oldfd);
	M0_ASSERT(result != -1);
	close(redir->ur_fd);
	clearerr(redir->ur_stream);
	fsetpos(redir->ur_stream, &redir->ur_pos);
}

M0_INTERNAL bool m0_error_mesg_match(FILE * fp, const char *mesg)
{
	enum {
		MAXLINE = 1025,
	};

	char line[MAXLINE];

	M0_PRE(fp != NULL);
	M0_PRE(mesg != NULL);

	fseek(fp, 0L, SEEK_SET);
	memset(line, '\0', MAXLINE);
	while (fgets(line, MAXLINE, fp) != NULL) {
		if (strncmp(mesg, line, strlen(mesg)) == 0)
			return true;
	}
	return false;
}

#ifdef ENABLE_FAULT_INJECTION

M0_INTERNAL int m0_ut_enable_fault_point(const char *str)
{
	int  rc = 0;
	int  i;
	char *s;
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

	if (str == NULL)
		return 0;

	s = m0_strdup(str);
	if (s == NULL)
		return -ENOMEM;

	while (true) {
		func = tag = type = data1 = data2 = NULL;

		token = strtok_r(s, ",", &token_saveptr);
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
			warn("Incorrect fault point specification\n");
			rc = -EINVAL;
			goto out;
		}

		data.fpd_type = m0_fi_fpoint_type_from_str(type);
		if (data.fpd_type == M0_FI_INVALID_TYPE) {
			warn("Incorrect fault point type '%s'\n", type);
			rc = -EINVAL;
			goto out;
		}

		if (data.fpd_type == M0_FI_RANDOM) {
			if (data1 == NULL) {
				warn("No probability was specified"
						" for 'random' FP type\n");
				rc = -EINVAL;
				goto out;
			}
			data.u.fpd_p = atoi(data1);
		} else if (data.fpd_type == M0_FI_OFF_N_ON_M) {
			if (data1 == NULL || data2 == NULL) {
				warn("No N or M was specified"
						" for 'off_n_on_m' FP type\n");
				rc = -EINVAL;
				goto out;
			}
			data.u.s1.fpd_n = atoi(data1);
			data.u.s1.fpd_m = atoi(data2);
		}

		m0_fi_enable_generic(func, tag, &data);

		/* s should be NULL for subsequent strtok_r(3) calls */
		s = NULL;
	}
out:
	m0_free(s);
	return rc;
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
				warn("Incorrect FP type '%s'\n", val);
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
			warn("Incorrect key '%s' in yaml file\n", key);
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
			m0_fi_enable_generic(strdup(func), m0_strdup(tag), &data);
		}

	return 0;
}

M0_INTERNAL int m0_ut_enable_fault_points_from_file(const char *file_name)
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
		warn("Failed to init yaml parser\n");
		rc = -EINVAL;
		goto fclose;
	}

	yaml_parser_set_input_file(&parser, f);

	rc = yaml_parser_load(&parser, &document);
	if (rc != 1) {
		warn("Incorrect YAML file\n");
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

int m0_ut_enable_fault_point(char *str)
{
	return 0;
}

int m0_ut_enable_fault_points_from_file(const char *file_name)
{
	return 0;
}

#endif /* ENABLE_FAULT_INJECTION */

/** @} end of ut group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
