AM_CFLAGS = $(PACKAGE_CFLAGS) -I$(top_srcdir)/include -I$(top_builddir)/include

lib_LTLIBRARIES = libbabeltrace-lttng-live.la

noinst_HEADERS = \
		 lttng-viewer-abi.h \
		 liblttng-live.h

libbabeltrace_lttng_live_la_SOURCES = \
	lttng-live.c liblttng-live.c

libbabeltrace_lttng_live_la_LIBADD = \
	$(top_builddir)/lib/libbabeltrace.la
