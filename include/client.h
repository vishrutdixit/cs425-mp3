#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netdb.h>

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <sstream>

#include "common.h"

class Client {

public:
    Client(unsigned int id);
    Client(const std::string& ip, const std::string& port, unsigned int id);
    int connect_to_server(const std::string& ip, const std::string& port);
    int get_socket_fd();
    bool is_connected();
    void close();

private:
    unsigned int process_id;
    int socket_fd;
    bool connected;

};

#endif
