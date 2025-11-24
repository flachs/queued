CFLAGS = -g

OFILES = dlc_option.o open_socket.o qecho.o qrun.o qconf.o queue.o enqueue.o queuesrv.o qservrst.o cpuinfo.o dlc_string.o proc.o tty_stat.o pidfile.o uid.o host.o joblink.o qdequeue.o time_format.o runimm.o tokens.o qps.o

CFILES = $(OFILES:.o=.c)

XIDLELIBS  =  -lX11 -lXext -lXss

INSTALL_DIR = /user/utility/packages/queued/bin

all: q TAGS

install: q
	cp q $(INSTALL_DIR)
	chmod ug+s $(INSTALL_DIR)/q

TAGS: $(CFILES)
	etags q.c $(CFILES)

q: q.c dlc_option.h q.h $(OFILES) trio/libtrio.a
	gcc -g -o $@ -pthread -Ltrio $< $(OFILES) -lm -ltrio -lpcre2-8

trio/libtrio.a : dlc_string.o dlc_string.h
	cd trio ; make

tokens.o : tokens.c qconf.h q.h

qservrst.o: qservrst.c q.h

joblink.o: joblink.c q.h

pidfile.o: pidfile.c q.h

cpuinfo.o : cpuinfo.c q.h

qconf.o : qconf.c qconf.h

qrun.o : qrun.c q.h

qecho.o : qecho.c q.h

queue.o : queue.c q.h dlc_string.h

enqueue.o : enqueue.c q.h

qdequeue.o : qdequeue.c q.h list.h

queuesrv.o : queuesrv.c q.h

runimm.o : runimm.c qconf.c q.h

dlc_string.o : dlc_string.c dlc_string.h

proc.o : proc.c proc.h

uid.o : uid.c list.h

host.o : host.c list.h


clean:
	rm -f $(OFILES) q TAGS
	cd trio ; make clean
