INCLUDES                 = -I. -I$(top_srcdir) -I$(top_srcdir)/include

RM_FOPS                  = rm_ff.h rm_ff.c

noinst_LTLIBRARIES       = libcolibri-rm.la
libcolibri_rm_la_SOURCES = $(RM_FOPS) rm_fops.c rm_fops.h \
			   rm.h rm.c rm_foms.c rm_foms.h

$(RM_FOPS): rm.ff                    \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

EXTRA_DIST       = rm_xc.ff

MOSTLYCLEANFILES = $(RM_FOPS)
