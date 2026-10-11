#include <format>
#include <iostream>
#include <string>
#include <cctype>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <cerrno>

#include "tcp.h"

bool send_all(int sock, const std::string& message) {
    std::size_t total = 0;

    while (total < message.size()) {
        ssize_t sent = send(sock, message.data() + total, message.size() - total, MSG_NOSIGNAL);

        if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::perror("send");
            return false;
        }

        if (sent == 0) {
            return false;
        }

        total += static_cast<std::size_t>(sent);
    }

    return true;
}

int receive_all(int fd, void *buffer, size_t sz){
    uint8_t * ptr = reinterpret_cast<uint8_t *>(buffer);
    std::size_t remain = sz;

    while (remain > 0) 
    {
        ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
         
        if (count == -1) {
            std::cerr << "receive_all(): count == -1";
            return 1;
        }
        if (count == 0) {
            std::cerr << "receive_all(): count == 0";
            return 1; 
        }
        ptr += (count);
        remain -= static_cast<size_t>(count);
    }

    return 0;
}
bool check_format(const std::string& floor)
{
    if (floor.size() > 3) {
        return false;
    }

    std::size_t start = 0;

    if (!floor.empty() && floor[0] == 'B') {
        start = 1;
    }

    for (std::size_t i = start; i < floor.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(floor[i]))) {
            return false;
        }
    }

    return true;
}


int main(int argc, char* argv[]) {
    // Program name + 2 user arguments = 3
    if (argc != 3) {
        std::cout << "Correct usage: ./call {source floor} {destination floor}\n";
        return 1;
    }

    const std::string source_floor = argv[1];
    const std::string destination_floor = argv[2];

    if (source_floor == destination_floor) {   
        std::cout << "You are already on that floor!\n";
        return 1;
    }

    // std::cout << source_floor << std::endl;
    //     std::cout << destination_floor <<  std::endl;


    if (! check_format(source_floor)) {
        std::cout << "Invalid floor(s) specified.\n";
        return 1;
    }

    if (! check_format(destination_floor)) {
        std::cout << "Invalid floor(s) specified.\n";
        return 1;
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // Print error when unable to create a socket
    if (sockfd == -1) {
        std::cout << std::format("Unable to connect to elevator system.\n");
        return 1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = static_cast<in_port_t>(htons(3000));
    
    int ret = inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (ret != 1) {
        std::cout << "Unable to connect to elevator system.\n";
        return 1;
    }

    // server is stack allocated memory and requires the address for pointer reinterpreation
    //(sockaddr *)(&server);
    // Connect to a running server
    if (connect(sockfd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) == -1) {
        std::cout << "Unable to connect to elevator system.\n";
        close(sockfd);

        return 1;
    }

    std::string message = std::format("CALL {} {}", source_floor, destination_floor);

    ret = send_message(sockfd, message);
    if (ret != 0) {
        return 1;
    }

    ret = receive_message(sockfd, message);

    if (ret != 0) {
        return 1;
    }

    if (message.starts_with("CAR")) {
    
        std::stringstream ss(message);
        std::string part;
        std::vector<std::string> result;

        // Split by the comma ',' character
        while (std::getline(ss, part, ' ')) {
            result.push_back(part);
        }

        std::cout << std::format("Car {} is arriving.\n", result[1]);
    } else if (message.starts_with("UNAVAILABLE")) {
        
        std::cout << "Sorry, no car is available to take this request.\n";
    } else {
        
        std::cout << "Unknown response from server: " << message << "\n";
    }

    if (shutdown(sockfd, SHUT_RDWR) == -1) {
        perror("shutdown()");
        return 1;
    }

    if (close(sockfd) == -1) {
        perror("close()");
        return 1;
    }
    
    return 1;
}
