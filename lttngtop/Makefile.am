AM_CFLAGS = $(PACKAGE_CFLAGS) -I$(top_srcdir)/include

bin_PROGRAMS = lttngtop

noinst_HEADERS = \
	lttngtoptypes.h \
	common.h \
	cputop.h \
	cursesdisplay.h \
	iostreamtop.h \
	live.h

lttngtop_SOURCES = \
	lttngtop.c \
	common.c \
	cursesdisplay.c \
	cputop.c \
	iostreamtop.c \
	live.c

lttngtop_LDFLAGS = -Wl,--no-as-needed
#lttngtop_LDADD = -lbabeltrace -lbabeltrace-ctf -llttngtop-helper -llttng-ctl -lurcu -llttngtopmmappacketseek -llttng-ust-ctl
lttngtop_LDADD = -lbabeltrace -lbabeltrace-ctf -llttng-ctl -lurcu -llttng-ust-ctl -lncurses -lpanel \
		   $(top_builddir)/lib/libbabeltrace.la \
		   $(top_builddir)/formats/ctf/libbabeltrace-ctf.la \
		   $(top_builddir)/compat/libcompat.la \
		   $(top_builddir)/formats/ctf-text/libbabeltrace-ctf-text.la \
		   $(top_builddir)/formats/ctf-metadata/libbabeltrace-ctf-metadata.la \
		   $(top_builddir)/formats/bt-dummy/libbabeltrace-dummy.la
