CC=gcc
RM=rm -f
MKDIR=mkdir -p
SED=sed
LN=ln -s
INSTALL=install
INSTALL_DATA=$(INSTALL) -m 444
FREE_CFLAGS=-std=c99 -fomit-frame-pointer -s -nostdlib -O2
CFLAGS=-std=c99 -fomit-frame-pointer -O2
PREF=/usr/local/
BIN=$(PREF)bin/
LIBSH=$(PREF)lib/sh

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

all: true false quickexec autorestart uptime-ng cat echo basename sleep unlink \
		 yes link args-length gpushd-server gpushd-client xte-bench readahead ln   \
		 rm cp mv ls cat mkdir test pwd kill par chmod seq clear chown rmdir base  \
		 sizeof crc32 sys_sync sync asciify qdaemon
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

chmod: chmod.c bsd.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) -DNO_HTABLE $^ -o $@

seq: seq.c record-invalid.c
	$(CC) $(CFLAGS) -lm $^ -o $@

chown: chown.c record-invalid.c fallback.c common-cmdline.c
	$(CC) $(CFLAGS) $^ -o $@
# end of bsd ports

rmdir: rmdir.c record-invalid.c
	$(CC) $(CFLAGS) $^ -o $@

gpushd-server: safe-call.c safe-call.h gpushd.h gpushd-server.c gpushd-common.c gpushd-common.h iobuf.c iobuf.h
	$(CC) $(CFLAGS) -pthread -lrt -DUSE_THREAD -DNDEBUG=1 $^ -o $@

gpushd-client: safe-call.c safe-call.h gpushd.h gpushd-client.c gpushd-common.c gpushd-common.h
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
	$(CC) $(CFLAGS) $^ -o $@

.PHONY : clean install

clean:
	$(RM) true false quickexec autorestart uptime-ng cat echo basename sleep \
				unlink yes args-length gpushd-server gpushd-client link xte-bench  \
				readahead ln rm cp mv ls cat mkdir test pwd kill par chmod seq     \
			  clear chown rmdir base sizeof crc32 sys_sync sync asciify qdaemon

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
	$(INSTALL) ln /bin
	$(INSTALL) rm /bin
	$(INSTALL) cp /bin
	$(INSTALL) mv /bin
	$(INSTALL) ls /bin
	$(INSTALL) mkdir /bin
	$(INSTALL) test /usr/bin
	$(INSTALL) sync /bin
	$(INSTALL) pwd /bin
	$(INSTALL) kill /bin
	$(INSTALL) chmod /bin
	$(INSTALL) seq /usr/bin
	$(INSTALL) clear /usr/bin
	$(INSTALL) chown /bin
	$(INSTALL) rmdir /bin

debian-install-core: all
	@sh debian-install-core.sh

debian-uninstall-core:
	@sh debian-uninstall-core.sh

install: all
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
	$(INSTALL) gpushd-server $(BIN)
	$(INSTALL) gpushd-client $(BIN)
	
	# scripts
	$(MKDIR)   $(LIBSH)
	$(INSTALL_DATA) gpushd.sh $(LIBSH)
	$(INSTALL) note-pop $(BIN)
	$(INSTALL) note-get $(BIN)
	$(INSTALL) note-push $(BIN)
	$(INSTALL) note-getall $(BIN)
	$(INSTALL) note-info $(BIN)
	$(SED) -i 's/##PREFIX##/\/usr\/local/g' \
  	$(BIN)note-pop                        \
		$(BIN)note-get                        \
	  $(BIN)note-push                       \
		$(BIN)note-getall                     \
		$(BIN)note-info
	$(LN) $(BIN)note-pop    $(BIN)todo-pop
	$(LN) $(BIN)note-get    $(BIN)todo-get
	$(LN) $(BIN)note-push   $(BIN)todo-push
	$(LN) $(BIN)note-getall $(BIN)todo-getall
	$(LN) $(BIN)note-info   $(BIN)todo-info

