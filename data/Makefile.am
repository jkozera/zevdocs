SUBDIRS = icons

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libdevhelp-3.0.pc

assistantdir = $(datadir)/devhelp/assistant
assistant_DATA = 		\
	assistant.js		\
	assistant.css

gsettings_SCHEMAS =				\
	org.gnome.devhelp.gschema.xml		\
	org.gnome.libdevhelp-3.gschema.xml
@GSETTINGS_RULES@

appstream_in_files = io.github.jkozera.ZevDocs.appdata.xml.in
appstream_XML = $(appstream_in_files:.xml.in=.xml)
@APPSTREAM_XML_RULES@

$(appstream_XML): $(appstream_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

desktopdir = $(datadir)/applications
desktop_in_file = io.github.jkozera.ZevDocs.desktop.in
desktop_DATA = io.github.jkozera.ZevDocs.desktop

$(desktop_DATA): $(desktop_in_file)
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

io.github.jkozera.ZevDocs.service: Makefile
	$(AM_V_GEN) (echo '[D-BUS Service]'; \
		     echo 'Name=io.github.jkozera.ZevDocs'; \
		     echo 'Exec=${bindir}/zevdocs --gapplication-service') > $@.tmp && \
		     mv $@.tmp $@

man_MANS = devhelp.1

EXTRA_DIST =			\
	libdevhelp-3.0.pc.in    \
	$(desktop_in_file)	\
	$(gsettings_SCHEMAS)    \
	$(assistant_DATA) \
	$(man_MANS) \
	$(appstream_in_files)

CLEANFILES =			\
	$(defaults_DATA)	\
	$(desktop_DATA) 	\
	$(appstream_XML)

-include $(top_srcdir)/git.mk
