snsdir                          = $(includedir)/colibri/sns
sns_HEADERS                     = ls_solve.h matvec.h parity_math.h \
                                  parity_ops.h repair.h sns.h

noinst_LTLIBRARIES		= libcolibri-sns.la
libcolibri_sns_la_SOURCES	= sns.c sns.h repair.c repair.h \
				  ls_solve.c matvec.c parity_math.c \
				  ls_solve.h matvec.h parity_math.h \
				  parity_ops.h parity_ops.c
INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include
EXTRA_DIST			= poolmach.c
