#ifndef COMMON_H
#define COMMON_H

#include <cstddef>
#include <errno.h>
#include <unistd.h>

int write_all_to_socket(int socket, const char *buffer, size_t count);
int read_all_from_socket(int socket, char *buffer, size_t count);

#endif
