

#include <stdio.h>
#include <helper.h>

void show_msg(int level, const char *fmt, ...)
{
        va_list ap;

        if (!level)
                return;

        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        va_end(ap);
}
