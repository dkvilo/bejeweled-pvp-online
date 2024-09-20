client:
	gcc -ggdb client.c `pkg-config --cflags --libs raylib` -lpthread -lm -o game_client

server:
	go build server.go