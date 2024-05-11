#CFLAGS=-O2 -Wall -g
NAME=membench
TARFILE=${NAME}.tar.gz

membench: membench.c
	gcc -o membench membench.c

clean:
	rm -f membench
	rm -f ${NAME}.tar.gz

${TARFILE}: clean
	 tar cCzf .. ${NAME}.tar.gz --exclude-vcs ${NAME} || true

rpm: ${TARFILE}
	 rpmbuild -ta ${NAME}.tar.gz 
