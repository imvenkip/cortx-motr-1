diff --git a/be/btree.c b/be/btree.c
index bc869eb64..4043594a1 100644
--- a/be/btree.c
+++ b/be/btree.c
@@ -757,6 +757,10 @@ merge_siblings(struct m0_be_btree *btree,
 	/* re-calculate checksum after all fields has been updated */
 	m0_format_footer_update(parent);
 
+	if (btree->bb_root == parent) {
+		M0_LOG(M0_ALWAYS, "b_nr_active=%d", parent->b_nr_active);
+	}
+
 	btree_node_free(n2, seg, btree, tx);
 
 	if (parent->b_nr_active == 0 && btree->bb_root == parent) {
diff --git a/be/ut/btree.c b/be/ut/btree.c
index c9f1b9edb..94d5e5191 100644
--- a/be/ut/btree.c
+++ b/be/ut/btree.c
@@ -29,6 +29,7 @@
 #include "lib/errno.h"     /* ENOENT */
 #include "be/ut/helper.h"
 #include "be/reg.h"        /* M0_BE_REG_GET_PTR */
+#include "be/btree_internal.h" /* XXX */
 #include "ut/ut.h"
 #ifndef __KERNEL__
 #include <stdio.h>	   /* sscanf */
@@ -208,8 +209,12 @@ btree_delete(struct m0_be_btree *t, struct m0_buf *k, int nr_left)
 		M0_UT_ASSERT(rc == 0);
 	}
 
+	M0_LOG(M0_ALWAYS, "###-{: nr_left=%d nr=%d root=%p root->nr_active=%u root->b_leaf=%d",
+	       nr_left, nr, t->bb_root, t->bb_root->b_nr_active, !!t->bb_root->b_leaf);
 	rc = M0_BE_OP_SYNC_RET_WITH(&op, m0_be_btree_delete(t, tx, &op, k),
 				    bo_u.u_btree.t_rc);
+	M0_LOG(M0_ALWAYS, "###-}: nr_left=%d nr=%d root=%p root->nr_active=%u root->b_leaf=%d",
+	       nr_left, nr, t->bb_root, t->bb_root->b_nr_active, !!t->bb_root->b_leaf);
 
 	if (--nr == 0 || nr_left == 0) {
 		m0_be_tx_close_sync(tx);
@@ -253,7 +258,9 @@ static void btree_delete_test(struct m0_be_btree *tree, struct m0_be_tx *tx)
 	M0_UT_ASSERT(rc == -ENOENT);
 
 	btree_dbg_print(tree);
-
+	M0_LOG(M0_ALWAYS, "1===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Delete random keys...");
 	M0_ALLOC_ARR(rand_keys, INSERT_COUNT);
 	M0_UT_ASSERT(rand_keys != NULL);
@@ -266,14 +273,18 @@ static void btree_delete_test(struct m0_be_btree *tree, struct m0_be_tx *tx)
 		rc = btree_delete(tree, &key, INSERT_COUNT - i - 2);
 		M0_UT_ASSERT(rc == 0);
 	}
-
+	M0_LOG(M0_ALWAYS, "2===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Make sure nothing deleted is left...");
 	for (i = 0; i < INSERT_COUNT; i+=2) {
 		sprintf(k, "%0*d", INSERT_KSIZE-1, rand_keys[i]);
 		rc = btree_delete(tree, &key, INSERT_COUNT - i - 2);
 		M0_UT_ASSERT(rc == -ENOENT);
 	}
-
+	M0_LOG(M0_ALWAYS, "3===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Insert back all deleted stuff...");
 	for (i = 0; i < INSERT_COUNT; i+=2) {
 		sprintf(k, "%0*d", INSERT_KSIZE-1, rand_keys[i]);
@@ -287,7 +298,9 @@ static void btree_delete_test(struct m0_be_btree *tree, struct m0_be_tx *tx)
 		rc = btree_insert(tree, &key, &val, INSERT_COUNT - i - 2);
 		M0_UT_ASSERT(rc == 0);
 	}
-
+	M0_LOG(M0_ALWAYS, "4===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Delete everything in random order...");
 	for (i = 0; i < INSERT_COUNT; i++) {
 		sprintf(k, "%0*d", INSERT_KSIZE-1, rand_keys[i]);
@@ -295,14 +308,18 @@ static void btree_delete_test(struct m0_be_btree *tree, struct m0_be_tx *tx)
 		rc = btree_delete(tree, &key, INSERT_COUNT - i - 1);
 		M0_UT_ASSERT(rc == 0);
 	}
-
+	M0_LOG(M0_ALWAYS, "5===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Make sure nothing is left...");
 	for (i = 0; i < INSERT_COUNT; i++) {
 		sprintf(k, "%0*d", INSERT_KSIZE-1, i);
 		rc = btree_delete(tree, &key, INSERT_COUNT - i - 1);
 		M0_UT_ASSERT(rc == -ENOENT);
 	}
-
+	M0_LOG(M0_ALWAYS, "6===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Insert everything back...");
 	for (i = 0; i < INSERT_COUNT; i++) {
 		sprintf(k, "%0*d", INSERT_KSIZE-1, rand_keys[i]);
@@ -317,7 +334,9 @@ static void btree_delete_test(struct m0_be_btree *tree, struct m0_be_tx *tx)
 		M0_UT_ASSERT(rc == 0);
 	}
 	m0_free(rand_keys);
-
+	M0_LOG(M0_ALWAYS, "7===========");
+	M0_LOG(M0_ALWAYS, "============");
+	M0_LOG(M0_ALWAYS, "============");
 	M0_LOG(M0_INFO, "Deleting [%04d, %04d)...", INSERT_COUNT/4,
 						    INSERT_COUNT*3/4);
 	for (i = INSERT_COUNT/4; i < INSERT_COUNT*3/4; ++i) {
diff --git a/be/ut/helper.c b/be/ut/helper.c
index 93da8c648..f001a32f3 100644
--- a/be/ut/helper.c
+++ b/be/ut/helper.c
@@ -337,8 +337,8 @@ void m0_be_ut_backend_cfg_default(struct m0_be_domain_cfg *cfg,
 		.bc_seg_nr                 = 0,
 		.bc_seg_nr_max             = 0x100,
 		.bc_pd_cfg = {
-			.bpc_mapping_type = M0_BE_PD_MAPPING_COMPAT,
-			/* .bpc_mapping_type = M0_BE_PD_MAPPING_PER_PAGE, */
+			/* .bpc_mapping_type = M0_BE_PD_MAPPING_COMPAT, */
+			.bpc_mapping_type = M0_BE_PD_MAPPING_PER_PAGE,
 			.bpc_pages_per_io       = 0x1000,
 			.bpc_io_sched_cfg = {
 				.bpdc_seg_io_nr = 0x4,
diff --git a/lib/trace.c b/lib/trace.c
index 5e30c6291..467faf694 100644
--- a/lib/trace.c
+++ b/lib/trace.c
@@ -213,6 +213,11 @@ M0_INTERNAL void m0_trace_allot(const struct m0_trace_descr *td,
 	struct m0_trace_buf_header *tbh = m0_logbuf_header;
 	register unsigned long      sp asm("sp"); /* stack pointer */
 
+
+	if (! ( (td->td_subsys & (1ULL << 7)) || (td->td_subsys & (1ULL << 2)) ))
+		return;
+
+
 	record_num = m0_atomic64_add_return(&tbh->tbh_rec_cnt, 1);
 
 	/*
