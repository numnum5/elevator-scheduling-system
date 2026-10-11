#include "tcp.h"

bool is_numeric(const std::string& value)
{
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character);
           });
}


int send_all(int fd, const void* buf, size_t size) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remain = size;
    while (remain > 0) {
        ssize_t sent = send(fd, ptr, remain, MSG_NOSIGNAL);

        if (sent == -1) {
            if (errno == EINTR) {
                continue; // Interrupted: retry the same remaining bytes
            }
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        ptr += sent;
        remain -= static_cast<size_t>(sent);
    }

    return 0;
}


int send_message(int fd, const std::string & message) {

    // std::cout << message << std::endl;

    uint32_t len = htonl(message.size());

    if (send_all(fd, &len, sizeof(len)) == -1) {
        return -1;
    }
    
    if (send_all(fd, message.c_str(), message.size()) == -1) {
        return -1; 
    }

    return 0;
}

static int receive_all(int fd, void *buffer, size_t sz){
    uint8_t * ptr = reinterpret_cast<uint8_t *>(buffer);
    std::size_t remain = sz;


    // std::cout << fd << " " << sz << std::endl;

    while (remain > 0) 
    {
        ssize_t count = recv(fd, buffer, sz, 0);
         
        if (count == -1) {
            if (errno == EINTR) {
                continue; // Interrupted: retry the same remaining bytes
            }

            if (errno == ENODEV) {
                return ENODEV;
            }

            return count;
        }
        if (count == 0) {
            // std::cerr << "receive_all(): count == 0";
            return 1; 
        }

        ptr += (count);
        remain -= static_cast<size_t>(count);
    }

    return 0;
}

int receive_message(int fd, std::string & message)
{
    uint32_t length = 0;
    int ret = receive_all(fd, &length, sizeof(length));

    if (ret != 0) {
        //  std::cerr << "receive_message(): count == -1" << errno << std::endl;
        return ret;
    }

    length = ntohl(length);

    // std::cout << "receive_message(): length:" << length << std::endl;

    char buffer[length + 1];

    ret = receive_all(fd, buffer, length);

    if (ret != 0) {
        return ret;
    }

    buffer[length] = '\0';

    message = buffer;

    return 0;
}