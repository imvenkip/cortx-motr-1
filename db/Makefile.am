dbdir                    = $(includedir)/colibri/db
db_HEADERS               = db.h extmap.h extmap_internal.h db_impl.h db_common.h

noinst_LTLIBRARIES       = libcolibri-db.la
libcolibri_db_la_SOURCES = db.c db.h extmap.c extmap.h extmap_internal.h \
                           db_impl.h db_common.c db_common.h

INCLUDES                 = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST               = linux_kernel/db.c \
                           linux_kernel/db_impl.h
