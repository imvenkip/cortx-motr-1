# This file contains build rules for *_xc.[hc] files, which are automatically
# generated from corresponding *.h files.

# *_xc.[ch] files contain generated xcode data structures for the given C
# structures from *.h files. For more information about xcode please refer to
# xcode/xcode.h and xcode/gccxml2xcode documentation.

# hide actual gccxml/gccxml2xcode build commands in silent make mode (V=0) and
# display them otherwise (V=1); take into account default verbosity level in
# configure (controlled by --enable-silent-rules option)
gccxml_verbose   = $(gccxml_verbose_$(V))
gccxml_verbose_  = $(gccxml_verbose_$(AM_DEFAULT_VERBOSITY))
gccxml_verbose_0 = @echo " GCCXML " $@;

gccxml2xcode_verbose   = $(gccxml2xcode_verbose_$(V))
gccxml2xcode_verbose_  = $(gccxml2xcode_verbose_$(AM_DEFAULT_VERBOSITY))
gccxml2xcode_verbose_0 = @echo " GCCXML2XC " $@;

# in order to suppress annoying automake warning "CFLAGS: non-POSIX variable name"
# -Wno-portability automake option should be set for this makefile
AUTOMAKE_OPTIONS = -Wno-portability

# gccxml doesn't like -Werror and --coverage options so we need to remove them
# from CFLAGS

%_xc.h %_xc.c: %.h
	$(gccxml_verbose)$(GCCXML) $(filter-out -Werror --coverage,$(CFLAGS)) -fxml=$(<:.h=.gccxml) $<
	$(gccxml2xcode_verbose)$(top_srcdir)/xcode/gccxml2xcode -i $(<:.h=.gccxml)

.PHONY: clean-xc
clean-xc:
	rm -f *.gccxml *_xc.[ch]

