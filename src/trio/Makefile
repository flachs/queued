SHELL = /bin/sh
CC	= gcc -std=gnu99
CFLAGS	= -g -O2 -DHAVE_STRUCT_SIGACTION -I. -DDEBUG -fPIC -fvisibility=hidden
OBJS	= triostr.o trio.o trionan.o
TARGETLIB	= libtrio.a
TARGETSHLIB	= $(TARGETLIB:.a=.so.2.0.0)
TARGETINCS	= trio.h triop.h triodef.h trionan.h triostr.h
LDFLAGS	= -L. -ltrio -lm
AR	= ar
RANLIB	= ranlib
ERASE	= rm -f
MKDIR	= mkdir -p
GENDOC	= doxygen
srcdir	= .
# VPATH doesn't seem to work with /usr/xpg4/bin/make on Solaris
# (use /usr/ccs/bin/make), and doesn't work on older Solaris make
# such as Solaris 2.6.


# Installation settings
INSTALL		= /bin/install -c
INSTALL_DATA	= ${INSTALL} -m 644
prefix		= /usr/local
exec_prefix	= ${prefix}
includedir	= ${prefix}/include
libdir		= ${exec_prefix}/lib

all: $(TARGETLIB) $(TARGETSHLIB) $(TARGET)

.PHONY: all check test install doc clean

$(srcdir)/configure: configure.in
	cd $(srcdir) && autoconf

Makefile: Makefile.in config.status
	CONFIG_COMMANDS= CONFIG_LINKS= CONFIG_HEADERS= \
	CONFIG_FILES=Makefile ./config.status

config.status: configure
	./config.status --recheck

check:	test
test:	regression
	./regression

install:	$(TARGETLIB) $(TARGETSHLIB)
	$(MKDIR) $(libdir)
	$(MKDIR) $(includedir)
	$(INSTALL_DATA) $(TARGETLIB) $(TARGETSHLIB) $(libdir)/
	for i in $(TARGETINCS);do \
	(set -x;$(INSTALL_DATA) $(srcdir)/$$i $(includedir)); \
	done

regression: regression.o $(TARGETLIB)
	$(CC) $(CFLAGS) regression.o $(LDFLAGS) -o $@

example: example.o $(TARGETLIB)
	$(CC) $(CFLAGS) example.o $(LDFLAGS) -o $@

compare: compare.o $(TARGETLIB)
	$(CC) $(CFLAGS) compare.o $(LDFLAGS) -o $@

userdef: userdef.o $(TARGETLIB)
	$(CC) $(CFLAGS) userdef.o $(LDFLAGS) -o $@

$(TARGETLIB): $(OBJS)
	$(AR) ruv $(TARGETLIB) $(OBJS)
	$(RANLIB) $(TARGETLIB)

$(TARGETSHLIB): $(TARGETLIB)
	$(CC) -lm -shared -Wl,-soname,$(patsubst %.so.2.0.0,%.so.2,$@) -Wl,--whole-archive,$< -Wl,--no-whole-archive -o $@

doc::
	(cd $(srcdir) && $(GENDOC) doc/trio.cfg)

clean:
	$(ERASE) *~ core core.* regression example $(TOBJS) $(OBJS) $(TARGET) $(TARGETLIB) $(TARGETSHLIB) example.o regression.o
