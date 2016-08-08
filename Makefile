CC=gcc
RM=rm -f
MKDIR=mkdir -p
FIND=find
SED=sed
LN=ln -sf
INSTALL=install
INSTALL_DATA=$(INSTALL) -m 444
FREE_CFLAGS=-std=c99 -fomit-frame-pointer -s -nostdlib -O2
CFLAGS=-std=c99 -fomit-frame-pointer -O2
PREF=/usr/local/
BIN=$(PREF)bin/

SUNIX_PATH=/usr/local/share/sunix

TLIBC_SRC := tlibc.h
SUBARCH   := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
		                  -e s/arm.*/arm/ -e s/sa110/arm/ \
				       -e s/s390x/s390/ -e s/parisc64/parisc/ \
				 -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
			      	 -e s/sh[234].*/sh/ )
ARCH      ?= $(SUBARCH)

ifdef DEBUG
	FREE_CFLAGS += -ggdb3 -O0 -DDEBUG
	CFLAGS      += -ggdb3 -O0 -DDEBUG
endif
ifeq ($(ARCH),i386)
        TLIBC_SRC += _i386_syscall.S _i386_syscall.c
endif
ifeq ($(ARCH),x86_64)
        TLIBC_SRC += _x86_64_syscall.S _x86_64_syscall.c
endif

CFLAGS += -DVERSION="\"$(shell cat VERSION)\""
commit = $(shell ./hash.sh)
ifneq ($(commit), UNKNOWN)
	CFLAGS += -DCOMMIT="\"$(commit)\""
	CFLAGS += -DPARTIAL_COMMIT="\"$(shell echo $(commit) | cut -c1-8)\""
endif

CRC32_CFLAGS = -DUSE_CRC32_C=1

# In FreeBSD systems, sometimes the correct cputype is not picked up.
# We check the log and enable it when it is available.
SSE42_SUPPORT=$(shell $(CC) -march=native -dM -E - < /dev/null | grep SSE4_2)
ifeq ($(SSE42_SUPPORT),)
  SSE42_SUPPORT=$(shell if [ -f /var/run/dmesg.boot ] ; then grep SSE4\.2 /var/run/dmesg.boot ; fi)
endif
ifneq ($(SSE42_SUPPORT),)
	CRC32_CFLAGS += -msse4.2
endif

all: true false quickexec autorestart uptime-ng cat echo basename sleep unlink \
		 yes link args-length xte-bench readahead ln                               \
		 rm cp mv ls cat mkdir test pwd kill par chmod seq clear chown rmdir base  \
		 sizeof crc32 sys_sync sync asciify qdaemon fpipe setpgrp setsid
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

sync: sync.c
	$(CC) $(CFLAGS) $^ -o $@

sys_sync: sys_sync.c $(TLIBC_SRC)
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

clear: clear.c $(TLIBC_SRC)
	$(CC) $(FREE_CFLAGS) $^ -o $@

# directly ported from bsd base system
ln: ln.c bsd.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) -DNO_HTABLE -DNO_STRMODE -DNO_SETMODE $^ -o $@

rm: rm.c bsd.c htable.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) -DNO_SETMODE $^ -o $@

cp: cp.c bsd.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) -DNO_HTABLE -DNO_STRMODE -DNO_SETMODE $^ -o $@

mv: mv.c bsd.c htable.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) $^ -DNO_SETMODE -o $@

ls: ls.c bsd.c htable.c record-invalid.c fallback.c common-cmdline.c iobuf.c iobuf_stdout.c
	$(CC) $(CFLAGS) $^ -DCOLORLS -DNO_SETMODE -ltinfo -o $@

cat: cat.c bsd.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) -DNO_HTABLE -DNO_STRMODE -DNO_SETMODE $^ -o $@

mkdir: mkdir.c bsd.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) $^ -DNO_HTABLE -DNO_STRMODE -o $@

test: test.c record-invalid.c
	$(CC) $(CFLAGS) $^ -o $@

pwd: pwd.c record-invalid.c
	$(CC) $(CFLAGS) $^ -o $@

kill: kill.c record-invalid.c
	$(CC) $(CFLAGS) $^ -o $@

setsid: setsid.c
	$(CC) $(CFLAGS) $^ -o $@

chmod: chmod.c bsd.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) -DNO_HTABLE $^ -o $@

seq: seq.c record-invalid.c
	$(CC) $(CFLAGS) -lm $^ -o $@

chown: chown.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) $^ -o $@
# end of bsd ports

rmdir: rmdir.c record-invalid.c
	$(CC) $(CFLAGS) $^ -o $@

setpgrp: setpgrp.c
	$(CC) $(CFLAGS) $^ -o $@

xte-bench: xte-bench.c iobuf.c
	$(CC) $(CFLAGS) -lm $^ -o $@

readahead: readahead.c
	$(CC) $(CFLAGS) $^ -o $@

par: par.c
	$(CC) $(CFLAGS) $^ -o $@

base: base.c safe-call.c
	$(CC) $(CFLAGS) $^ -o $@

asciify: asciify.c iobuf.c iobuf_stdout.c
	$(CC) $(CFLAGS) $^ -o $@

qdaemon: qdaemon.c
	$(CC) $(CFLAGS) $^ -o $@

sizeof: sizeof.c iobuf.c
	$(CC) $(CFLAGS) $^ -o $@

crc32: crc32-file.c crc32.c
	$(CC) $(CFLAGS) $(CRC32_CFLAGS) $^ -o $@

fpipe: fpipe.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY : clean install

clean:
	$(RM) true false quickexec autorestart uptime-ng cat echo basename sleep    \
				unlink yes args-length link xte-bench                                 \
				readahead ln rm cp mv ls cat mkdir test pwd kill par chmod seq fpipe  \
				clear chown rmdir base sizeof crc32 sys_sync sync asciify qdaemon     \
				setpgrp setsid

core-install: all
	$(MKDIR) $(SUNIX_PATH)/usr/bin
	$(MKDIR) $(SUNIX_PATH)/usr/sbin
	$(MKDIR) $(SUNIX_PATH)/bin
	$(MKDIR) $(SUNIX_PATH)/sbin
	$(INSTALL) true $(SUNIX_PATH)/bin
	$(INSTALL) false $(SUNIX_PATH)/bin
	$(INSTALL) cat $(SUNIX_PATH)/bin
	$(INSTALL) echo $(SUNIX_PATH)/bin
	$(INSTALL) basename $(SUNIX_PATH)/usr/bin
	$(INSTALL) sleep $(SUNIX_PATH)/bin
	$(INSTALL) unlink $(SUNIX_PATH)/usr/bin
	$(INSTALL) yes $(SUNIX_PATH)/usr/bin
	$(INSTALL) ln $(SUNIX_PATH)/bin
	$(INSTALL) rm $(SUNIX_PATH)/bin
	$(INSTALL) cp $(SUNIX_PATH)/bin
	$(INSTALL) mv $(SUNIX_PATH)/bin
	$(INSTALL) ls $(SUNIX_PATH)/bin
	$(INSTALL) mkdir $(SUNIX_PATH)/bin
	$(INSTALL) test $(SUNIX_PATH)/usr/bin
	$(INSTALL) sync $(SUNIX_PATH)/bin
	$(INSTALL) pwd $(SUNIX_PATH)/bin
	$(INSTALL) kill $(SUNIX_PATH)/bin
	$(INSTALL) chmod $(SUNIX_PATH)/bin
	$(INSTALL) seq $(SUNIX_PATH)/usr/bin
	$(INSTALL) clear $(SUNIX_PATH)/usr/bin
	$(INSTALL) chown $(SUNIX_PATH)/bin
	$(INSTALL) rmdir $(SUNIX_PATH)/bin
	$(INSTALL) setsid $(SUNIX_PATH)/usr/bin
	$(LN) $(SUNIX_PATH)/usr/bin/test $(SUNIX_PATH)/usr/bin/\[
	$(FIND) $(SUNIX_PATH) -exec chmod 755 {} \;

debian-install-core: all
	@sh debian-install-core.sh

debian-uninstall-core:
	@sh debian-uninstall-core.sh

install: all
	$(INSTALL) setpgrp $(BIN)
	$(INSTALL) qdaemon $(BIN)
	$(INSTALL) asciify $(BIN)
	$(INSTALL) sizeof $(BIN)
	$(INSTALL) base $(BIN)
	$(INSTALL) par $(BIN)
	$(INSTALL) sys_sync $(BIN)
	$(INSTALL) crc32 $(BIN)
	$(INSTALL) readahead $(BIN)
	$(INSTALL) xte-bench $(BIN)
	$(INSTALL) quickexec $(BIN)
	$(INSTALL) autorestart $(BIN)
	$(INSTALL) uptime-ng $(BIN)
	$(INSTALL) args-length $(BIN)
	$(INSTALL) fpipe $(BIN)
