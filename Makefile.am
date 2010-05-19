
INCLUDES = -I$(includedir)/infiniband $(GLIB_CFLAGS)

lib_LTLIBRARIES = src/libopensmskummeeplugin.la

DBGFLAGS = -g

src_libopensmskummeeplugin_la_CFLAGS = -Wall $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1 -Wno-deprecated-declarations

if HAVE_LD_VERSION_SCRIPT
    libopensmskummeeplugin_version_script = -Wl,--version-script=$(srcdir)/libopensmskummeeplugin.map
else
    libopensmskummeeplugin_version_script =
endif

src_libopensmskummeeplugin_la_SOURCES = src/opensmskummeeplugin.c \
					src/sql_queries.c
src_libopensmskummeeplugin_la_LDFLAGS = -version-info $(libopensmskummeeplugin_api_version) \
	-export-dynamic $(libopensmskummeeplugin_version_script)
src_libopensmskummeeplugin_la_LIBADD = -L$(libdir) -L$(libdir)/mysql -lrt -losmcomp -lmysqlclient_r $(GLIB_LIBS)
src_libopensmskummeeplugin_la_DEPENDENCIES = $(srcdir)/libopensmskummeeplugin.map

sbin_PROGRAMS = src/opensmskummeequery

src_opensmskummeequery_CFLAGS = $(DBGFLAGS)
src_opensmskummeequery_SOURCES = src/opensmskummeequery.c
src_opensmskummeequery_LDFLAGS = -L$(libdir) -L$(libdir)/mysql -lmysqlclient_r -losmcomp

sysconf_DATA = etc/libopensmskummee.conf

sbin_SCRIPTS = scripts/opensmskummee_create_db.pl \
               scripts/opensmskummee_update_thresh.pl

man_MANS = man/opensmskummeequery.8

# headers are distributed as part of the include dir
EXTRA_DIST = $(srcdir)/libopensmskummeeplugin.spec.in \
				 $(srcdir)/libopensmskummeeplugin.map \
				 $(srcdir)/src/*.h \
				 $(srcdir)/etc/libopensmskummee.conf \
				 $(srcdir)/config \
				 $(srcdir)/ChangeLog \
				 $(srcdir)/INSTALL \
				 $(srcdir)/README \
			    $(srcdir)/META \
			    $(srcdir)/DISCLAIMER \
			    $(srcdir)/scripts \
				 $(man_MANS)

dist-hook: libopensmskummeeplugin.spec
	cp libopensmskummeeplugin.spec $(distdir)

install-exec-hook:
	cat $(DESTDIR)$(sbindir)/opensmskummee_update_thresh.pl | sed '1,60 s,EXEC_PREFIX_NOT_SET,$(sbindir),' > /tmp/opensmskummee_update_thresh.pl
	$(sbinPROGRAMS_INSTALL) /tmp/opensmskummee_update_thresh.pl $(DESTDIR)$(sbindir)/opensmskummee_update_thresh.pl

install-data-hook:
	chmod 640 $(DESTDIR)$(sysconfdir)/libopensmskummee.conf

