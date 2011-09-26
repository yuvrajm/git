all:: git-credential-osxkeychain

CC = gcc
RM = rm -f
CFLAGS = -O2 -Wall

git-credential-osxkeychain: git-credential-osxkeychain.o
	$(CC) -o $@ $< -Wl,-framework -Wl,Security

git-credential-osxkeychain.o: git-credential-osxkeychain.c
	$(CC) -c $(CFLAGS) $<

clean:
	$(RM) git-credential-osxkeychain git-credential-osxkeychain.o
