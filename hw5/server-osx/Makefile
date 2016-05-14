all: server

server: server.c
	gcc -Wall -Werror -Wno-deprecated-declarations -g -o server server.c -pthread -l sqlite3 -lssl -lcrypto 

clean:
	rm -f *~ *.o server