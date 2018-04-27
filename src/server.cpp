#include "server.h"

/**
 * Server constructor
 */
Server::Server(std::string port, int max_clients){
	start(port, max_clients);
}

/**
 * Begins listening for incoming connections on the passed port
 */
void Server::start(const std::string& port, int max_clients){
	int error;
	this->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(this->socket_fd < 0){ exit(1); }
	int optval = 1;
	setsockopt(this->socket_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	setsockopt(this->socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    fcntl(this->socket_fd, F_SETFL, fcntl(this->socket_fd, F_GETFL, 0) | O_NONBLOCK); // Set to non-blocking

	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_STREAM; // Streaming uses TCP
	hints.ai_flags = AI_PASSIVE; // Servers are passive

	error = getaddrinfo(NULL, port.c_str(), &hints, &result);
	if (error != 0) {
		fprintf(stderr, "server getaddrinfo: %s\n", gai_strerror(error));
		exit(1);
	}

	if (bind(this->socket_fd, result->ai_addr, result->ai_addrlen) != 0) {
		perror("bind()");
		exit(1);
	}

	if (listen(this->socket_fd, max_clients) != 0) {
		perror("listen()");
		exit(1);
	}

	freeaddrinfo(result);
}

/**
 * Accepts a new client if there are any attempting to connect.
 * Returns file descriptor which can be used to read from the socket connection
 */
int Server::accept_client(){
	int client_fd = accept(this->socket_fd, NULL, NULL);
	if(client_fd >= 0){
		int flags = fcntl(client_fd, F_GETFL, 0);
		fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
		// std::cout << "Client with fd " << client_fd << " connected." << std::endl;
		return client_fd;
	}
	return -1;
}

/**
 * Closes connections with clients and finally closes the listening socket
 */
void Server::close(){
	::close(this->socket_fd);
}


/**
 * Returns the file descriptor of listening socket of the server instance
 */
int Server::get_socket_fd(){
	return this->socket_fd;
}
