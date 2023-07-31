PROGS = paginator taskinator
OBJS = ${PROGS:=.o} x.o
SRCS = ${OBJS:.o=.c}
MANS = ${PROGS:=.1}

PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC = /usr/local/include
LOCALLIB = /usr/local/lib
X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

DEFS = -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -D_BSD_SOURCE
INCS = -I${LOCALINC} -I${X11INC}
LIBS = -L${LOCALLIB} -L${X11LIB} -lX11 -lXrender -lXpm
PROG_CFLAGS = -std=c99 -pedantic ${DEFS} ${INCS} ${CFLAGS} ${CPPFLAGS}
PROG_LDFLAGS = ${LIBS} ${LDLIBS} ${LDFLAGS}

bindir = ${DESTDIR}${PREFIX}/bin
mandir = ${DESTDIR}${MANPREFIX}/man1

all: ${PROGS}

${PROGS}: ${@:=.o} x.o
	${CC} -o $@ ${@:=.o} x.o ${PROG_LDFLAGS}

.c.o:
	${CC} ${PROG_CFLAGS} -o $@ -c $<

tags: ${SRCS}
	ctags ${SRCS}

lint: ${SRCS}
	-mandoc -T lint -W warning ${MANS}
	-clang-tidy ${SRCS} -- -std=c99 ${PROG_CFLAGS}

clean:
	rm -f ${OBJS} ${PROGS} ${PROGS:=.core} tags

install: all
	mkdir -p ${bindir}
	mkdir -p ${mandir}
	for file in ${PROGS} ; do install -m 755 "$$file" ${bindir}/"$$file" ; done
	for file in ${MANS} ; do install -m 644 "$$file" ${mandir}/"$$file" ; done

uninstall:
	-for file in ${PROGS} ; do rm ${bindir}/"$$file" ; done
	-for file in ${MANS} ; do rm ${mandir}/"$$file" ; done

.PHONY: all clean install uninstall lint
