build_server:
	@gcc -o server server.c -lpthread

build_client:
	@gcc -o client client.c -lpthread
