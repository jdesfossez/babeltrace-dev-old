AM_CFLAGS = $(PACKAGE_CFLAGS) -I$(top_srcdir)/include -I$(top_builddir)/include -I$(top_srcdir)

lib_LTLIBRARIES = libbabeltrace-lttng-state.la

noinst_HEADERS = \
		 lttng-viewer-abi.h \
		 lttng-state.h \
		 lttng-state-track.h

libbabeltrace_lttng_state_la_SOURCES = \
	lttng-state-plugin.c lttng-state-comm.c lttng-state-track.c

# Request that the linker keeps all static libraries objects.
libbabeltrace_lttng_state_la_LDFLAGS = \
	-Wl,--no-as-needed -version-info $(BABELTRACE_LIBRARY_VERSION)

libbabeltrace_lttng_state_la_LIBADD = \
	$(top_builddir)/lib/libbabeltrace.la
