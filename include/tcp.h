#include <format>
#include <iostream>
#include <string>
#include <cctype>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>

bool is_numeric(const std::string& value);
int send_message(int fd, const std::string& message);;
int receive_message(int fd, std::string & message);