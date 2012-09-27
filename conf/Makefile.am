include $(top_srcdir)/Makefile.xc

_OBJS_SRC = objs/common.h objs/common.c objs/dir.c objs/profile.c   \
            objs/filesystem.c objs/service.c objs/node.c objs/nic.c \
            objs/sdev.c objs/partition.c

noinst_LTLIBRARIES = libcolibri-conf.la
libcolibri_conf_la_SOURCES = \
        onwire.h onwire_xc.c buf_ext.h buf_ext.c confc.h confc.c obj.h obj.c \
	obj_ops.h obj_ops.c $(_OBJS_SRC) preload.h preload.c reg.h reg.c \
	conf_xcode.c xcode.h reg_debug.c

INCLUDES = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include \
           -iquote /usr/include

clean-local: clean-xc
