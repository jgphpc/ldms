include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_THERMAL_ZONES
dist_man7_MANS += ldms-sampler_thermal_zones.man
libthermal_zones_la_SOURCES = thermal_zones.c
libthermal_zones_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libthermal_zones.la
endif

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-sampler_thermal_zones.rst
