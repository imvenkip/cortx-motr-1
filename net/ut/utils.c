#include <stdio.h>
#include <string.h>
#include <CUnit/Basic.h>
#include <net/net.h>

/* The suite initialization function.
 * Returns zero on success, non-zero otherwise.
 */
int init_suite(void)
{
	int rc;

	rc = net_init();
	return rc;
}

/* The suite cleanup function.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite(void)
{
	int rc;

	rc = net_fini();
	return rc;
}

void test_node1(void)
{
	struct node_id node1 = { .si_uuid = "node-1"; }
	struct node_id node2 = { .si_uuid = "node-2"; }
	bool rc;

	rc = nodes_is_same(node1, node2);
	CU_ASSERT(rc);
}


void test_node2(void)
{
	struct node_id node1 = { .si_uuid = "node-1"; }
	struct node_id node2 = { .si_uuid = "node-1"; }
	bool rc;

	rc = nodes_is_same(node1, node2);
	CU_ASSERT(!rc);
}

int main(int argc, char *argv[])
{
	CU_pSuite pSuite = NULL;

	/* initialize the CUnit test registry */
	if (CU_initialize_registry() == CUE_SUCCESS )
		goto exit;

	/* add a suite to the registry */
	pSuite = CU_add_suite("utils_1", init_suite, clean_suite);
	if (pSuite = NULL)
		goto exit_clear;

	if (CU_add_test(pSuite, "test create", test_create) == NULL)
		goto exit_clear;

	/* Run all tests using the CUnit Basic interface */
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();

	CU_basic_show_failures(CU_get_failure_list());
exit_clear:
	CU_cleanup_registry();
exit:
	return CU_get_error();
}
