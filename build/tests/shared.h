#pragma once

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string>

enum class Status
{
    OPENING,
    OPEN,
    CLOSING,
    CLOSED,
    BETWEEN
};

inline const char *status_name(Status status)
{
  switch (status) {
    case Status::OPENING: return "Opening";
    case Status::OPEN: return "Open";
    case Status::CLOSING: return "Closing";
    case Status::CLOSED: return "Closed";
    case Status::BETWEEN: return "Between";
  }
  return "Invalid";
}

inline Status parse_status(const char *status)
{
  if (strcmp(status, "Opening") == 0) return Status::OPENING;
  if (strcmp(status, "Open") == 0) return Status::OPEN;
  if (strcmp(status, "Closing") == 0) return Status::CLOSING;
  if (strcmp(status, "Between") == 0) return Status::BETWEEN;
  return Status::CLOSED;
}

inline int32_t parse_floor(const char *floor)
{
  return floor[0] == 'B' ? -atoi(floor + 1) : atoi(floor);
}

inline void format_floor(char *out, int32_t floor)
{
  if (floor < 0) sprintf(out, "B%d", -floor);
  else sprintf(out, "%d", floor);
}


struct car_shared_mem {
  pthread_mutex_t mutex;           // Locked while accessing struct contents
  pthread_cond_t cond;             // Signalled when the contents change
    int32_t current_floor;
    int32_t destination_floor;
    Status status;
//   char current_floor[4];           // C string in the range B99-B1 and 1-999
//   char destination_floor[4];       // Same format as above
//   char status[8];                  // C string indicating the elevator's status
  uint8_t open_button;             // 1 if open doors button is pressed, else 0
  uint8_t close_button;            // 1 if close doors button is pressed, else 0
  uint8_t door_obstruction;        // 1 if obstruction detected, else 0
  uint8_t overload;                // 1 if overload detected
  uint8_t emergency_stop;          // 1 if stop button has been pressed, else 0
  uint8_t individual_service_mode; // 1 if in individual service mode, else 0
  uint8_t emergency_mode;          // 1 if in emergency mode, else 0
};

void recv_looped(int fd, void *buf, size_t sz)
{
  char *ptr = static_cast<char *>(buf);
  size_t remain = sz;

  while (remain > 0) {
    ssize_t received = read(fd, ptr, remain);
    if (received == -1) {
      perror("read()");
      exit(1);
    }
    ptr += received;
    remain -= received;
  }
}

char *receive_msg(int fd)
{
  uint32_t nlen;
  recv_looped(fd, &nlen, sizeof(nlen));
  uint32_t len = ntohl(nlen);

  char *buf = static_cast<char *>(malloc(len + 1));
  buf[len] = '\0';
  recv_looped(fd, buf, len);
  return buf;
}

void send_looped(int fd, const void *buf, size_t sz)
{
  const char *ptr = static_cast<const char *>(buf);
  size_t remain = sz;

  while (remain > 0) {
    ssize_t sent = write(fd, ptr, remain);
    if (sent == -1) {
      perror("write()");
      exit(1);
    }
    ptr += sent;
    remain -= sent;
  }
}

void send_message(int fd, const char *buf)
{
  uint32_t len = htonl(strlen(buf));
  send_looped(fd, &len, sizeof(len));
  send_looped(fd, buf, strlen(buf));
}

void msg(const char *string)
{
  printf("### %s\n    ", string);
  fflush(stdout);
}

void reset_shm(car_shared_mem *s)
{
  pthread_mutex_lock(&s->mutex);
  size_t offset = offsetof(car_shared_mem, current_floor);
  memset((char *)s + offset, 0, sizeof(*s) - offset);

  s->status = Status::CLOSED;
  s->current_floor = 1;
  s->destination_floor = 1;
  pthread_mutex_unlock(&s->mutex);
}

inline void display_shared_mem(const car_shared_mem *s)
{
  const auto floor_string = [](int32_t floor) {
    if (floor < 0) {
      return std::string("B") + std::to_string(-static_cast<int64_t>(floor));
    }
    return std::to_string(floor);
  };

  const std::string current_floor = floor_string(s->current_floor);
  const std::string destination_floor = floor_string(s->destination_floor);

  printf("Current state: {%s, %s, %s, %d, %d, %d, %d, %d, %d, %d}\n",
    current_floor.c_str(),
    destination_floor.c_str(),
    status_name(s->status),
    s->open_button,
    s->close_button,
    s->door_obstruction,
    s->overload,
    s->emergency_stop,
    s->individual_service_mode,
    s->emergency_mode
  );
}

void init_shm(car_shared_mem *s)
{
  pthread_mutexattr_t mutattr;
  pthread_mutexattr_init(&mutattr);
  pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&s->mutex, &mutattr);
  pthread_mutexattr_destroy(&mutattr);

  pthread_condattr_t condattr;
  pthread_condattr_init(&condattr);
  pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(&s->cond, &condattr);
  pthread_condattr_destroy(&condattr);

  reset_shm(s);
}
