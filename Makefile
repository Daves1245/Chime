all: src/client.c src/server.c
	     gcc -Wall src/message.c src/client.c -o bin/client -lpthread -lm
			 gcc -Wall src/message.c src/server.c -o bin/server -lpthread -lm


