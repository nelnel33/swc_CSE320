all: chat client server

chat: chat.c
	gcc -Wall -Werror -g -o chat chat.c -pthread -lncurses

client: client.c
	gcc -Wall -Werror -g -o client client.c -pthread

server: server.c
	gcc -Wall -Werror -g -o server server.c -pthread -l sqlite3 -lssl -lcrypto 

clean:
	rm -f *~ *.o chat client server