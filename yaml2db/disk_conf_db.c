#include "yaml2db/disk_conf_db.h"
#include "lib/arith.h"

/* DB Table ops */
static int test_key_cmp(struct c2_table *table,
                        const void *key0, const void *key1)
{
        const uint64_t *u0 = key0;
        const uint64_t *u1 = key1;

        return C2_3WAY(*u0, *u1);
}

/* Table ops for disk table */
const struct c2_table_ops c2_conf_disk_table_ops = {
        .to = {
                [TO_KEY] = { .max_size = 256 },
                [TO_REC] = { .max_size = 256 }
        },
        .key_cmp = test_key_cmp
};

