#pragma once

struct addrinfo;

int create_dgram_socket(const char* address, const char* port, addrinfo* res_addr);

int create_server(const char* port);

int create_client(const char* address, const char* port, addrinfo* res_addr);
