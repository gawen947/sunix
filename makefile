CC=gcc
RM=rm -f
INSTALL=install
FREE_CFLAGS=-std=c99 -s -nostdlib -fomit-frame-pointer -O2
CFLAGS=-std=c99 -fomit-frame-pointer -O2
PREF=/usr/local/
BIN=$(PREF)bin/

TLIBC_SRC := tlibc.h
SUBARCH   := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
		                  -e s/arm.*/arm/ -e s/sa110/arm/ \
				       -e s/s390x/s390/ -e s/parisc64/parisc/ \
      				 -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
			      	 -e s/sh[234].*/sh/ )
ARCH      ?= $(SUBARCH)

ifeq ($(ARCH),i386)
        TLIBC_SRC += _i386_syscall.S _i386_syscall.c
endif
ifeq ($(ARCH),x86_64)
        TLIBC_SRC += _x86_64_syscall.S _x86_64_syscall.c
endif

all: true false quickexec autorestart uptime-ng cat echo basename sleep unlink \
		 yes link args-length gpushd-server gpushd-client xte-bench readahead ln   \
		 rm cp mv
	strip $^

true: true.c common.h
	$(CC) $(FREE_CFLAGS) $^ -o $@

false: false.c common.h
	$(CC) $(FREE_CFLAGS) $^ -o $@

autorestart: autorestart.c
	$(CC) $(CFLAGS) $^ -o $@

quickexec: quickexec.c
	$(CC) $(CFLAGS) $^ -o $@

uptime-ng: uptime-ng.c
	$(CC) $(CFLAGS) $^ -o $@

cat: cat.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

echo: echo.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

basename: basename.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

sleep: sleep.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

unlink: unlink.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

yes: yes.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

link: link.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

args-length: args-length.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

# directly ported from bsd base system
ln: ln.c bsd.c
	$(CC) $(CFLAGS) -DNO_HTABLE -DNO_STRMODE $^ -o $@

rm: rm.c bsd.c htable.c
	$(CC) $(CFLAGS) $^ -o $@

cp: cp.c bsd.c
	$(CC) $(CFLAGS) -DNO_HTABLE -DNO_STRMODE $^ -o $@

mv: mv.c bsd.c htable.c
	$(CC) $(CFLAGS) $^ -o $@
# end of bsd ports 

gpushd-server: safe-call.c safe-call.h gpushd.h gpushd-server.c gpushd-common.c gpushd-common.h
	$(CC) $(CFLAGS) -pthread -lrt -DUSE_THREAD=1 -DNDEBUG=1 $^ -o $@

gpushd-client: safe-call.c safe-call.h gpushd.h gpushd-client.c gpushd-common.c gpushd-common.h
	$(CC) $(CFLAGS) $^ -o $@

xte-bench: xte-bench.c
	$(CC) $(CFLAGS) -lm $^ -o $@

readahead: readahead.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY : clean install

clean:
	$(RM) true false quickexec autorestart uptime-ng cat echo basename sleep \
				unlink yes args-length gpushd-server gpushd-client link xte-bench  \
				readahead ln rm cp mv

core-install: all
	@echo "Installing core files, hope you've backed up coreutils"
	$(INSTALL) true /bin
	$(INSTALL) false /bin
	$(INSTALL) cat /bin
	$(INSTALL) echo /bin
	$(INSTALL) basename /usr/bin
	$(INSTALL) sleep /bin
	$(INSTALL) unlink /usr/bin
	$(INSTALL) yes /usr/bin
	$(INSTALL) ln /bin/ln
	$(INSTALL) rm /bin/rm
	$(INSTALL) cp /bin/cp
	$(INSTALL) mv /bin/mv

debian-install-core: all
	@sh debian-install-core.sh

debian-uninstall-core: all
	@sh debian-uninstall-core.sh

install: all
	$(INSTALL) readahead $(BIN)
	$(INSTALL) xte-bench $(BIN)
	$(INSTALL) quickexec $(BIN)
	$(INSTALL) autorestart $(BIN)
	$(INSTALL) uptime-ng $(BIN)
	$(INSTALL) args-length $(BIN)
	$(INSTALL) gpushd-server $(BIN)
	$(INSTALL) gpushd-client $(BIN)
