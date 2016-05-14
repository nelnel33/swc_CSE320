CC = gcc
CFLAGS = -g -Wall -Werror

SEROBJ = server.o sfwrite.o

CLIOBJ = client.o sfwrite.o

CHATOBJ = chat.o sfwrite.o

AUDOBJ = auditor.o parser.o

all: chat client server auditor

server.o: server.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

chat.o: chat.c
	$(CC) $(CFLAGS) -c chat.c

sfwrite.o: sfwrite.c sfwrite.h
	$(CC) $(CFLAGS) -c sfwrite.c

auditor.o: auditor.c
	$(CC) $(CFLAGS) -c auditor.c

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c


server: $(SEROBJ)
	$(CC) $(CFLAGS) $(SEROBJ) -o server -pthread -l sqlite3 -lssl -lcrypto

client: $(CLIOBJ)
	$(CC) $(CFLAGS) $(CLIOBJ) -o client -pthread

chat: $(CHATOBJ)
	$(CC) $(CFLAGS) $(CHATOBJ) -o chat -pthread -lncurses

auditor: $(AUDOBJ)
	$(CC) $(CFLAGS) $(AUDOBJ) -o auditor -lncurses

.PHONY: clean
clean:
	rm -f *~ *.o *.log