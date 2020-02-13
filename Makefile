all: src/client.c src/server.c
	     gcc -Wall src/client.c -o bin/client -lpthread
			 gcc -Wall src/server.c -o bin/server -lpthread


