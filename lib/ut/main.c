#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>

extern void test_list(void);
extern void test_bitops(void);
extern void test_bitmap(void);

int main(int argc, char *argv[])
{
        test_list();
        test_bitops();
        test_bitmap();
        return 0;
}
