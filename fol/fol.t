
PREFIX	fol

INCLUDE #include <pthread.h>
INCLUDE #include <stdlib.h>
INCLUDE #include <stdarg.h>
INCLUDE #include <err.h>
INCLUDE #include <db.h>
INCLUDE #include <dbinc/db_swap.h>
INCLUDE #include <db_int.h>
INCLUDE #include "dbtypes.h"
INCLUDE #include "fol.h"

BEGIN create 48 20001
ARG      fc_epoch0  u32    lx
ARG      fc_epoch1  u32    lx
ARG      fc_lsn0    u32    lx
ARG      fc_lsn1    u32    lx
ARG      fc_pfid0   u32    lx
ARG      fc_pfid1   u32    lx
ARG      fc_pfid2   u32    lx
ARG      fc_pfid3   u32    lx
ARG      fc_pver0   u32    lx
ARG      fc_pver1   u32    lx
ARG      fc_flags   u32    lx
ARG      fc_uid     u32    lx
ARG      fc_gid     u32    lx
ARG      fc_cfid0   u32    lx
ARG      fc_cfid1   u32    lx
ARG      fc_cfid2   u32    lx
ARG      fc_cfid3   u32    lx
ARG      fc_cver0   u32    lx
ARG      fc_cver1   u32    lx
DBT      fc_name    DBT     s
END
