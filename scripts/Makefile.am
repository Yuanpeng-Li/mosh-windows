EXTRA_DIST = wrap-compiler-for-flag-check mosh.pl CMakeLists.txt install-launcher.sh
if BUILD_CLIENT
  bin_SCRIPTS = mosh
endif
CLEANFILES = $(bin_SCRIPTS)

mosh:	mosh.pl ../VERSION.stamp Makefile
	perl -Mdiagnostics -c $(srcdir)/mosh.pl
	@sed -e "s/\@VERSION\@/`cat ../VERSION.stamp`/" -e "s/\@PACKAGE_STRING\@/@PACKAGE_STRING@/" $(srcdir)/mosh.pl > mosh
	@chmod a+x mosh
