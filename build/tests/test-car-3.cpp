#include "shared.h"

// Tester for car (with controller. No shared memory tests.)

#define DELAY 50000 // 50ms
#define MILLISECOND 1000 // 1ms

pid_t car(const char *, const char *, const char *, const char *);
void cleanup(pid_t);
void *server(void *);
void server_init();
void test_recv(int, const char *);
std::string readable_status(const char *);

int server_fd;

int main()
{
  shm_unlink("/carTest"); // Remove shm object if it exists

  pid_t p;

  // Testing with server launched before car
  server_init();

  p = car("Test", "1", "8", "20");
  
  int fd;
  fd = accept(server_fd, NULL, NULL);
  test_recv(fd, "RECV: CAR Test 1 8");

  // Receive first update (should come immediately)
  test_recv(fd, "RECV: STATUS Closed 1 1");

  // Send the car up to floor 4
  send_message(fd, "FLOOR 4");
  {
    msg("RECV: STATUS Closed 1 4 (or Between 1 4)");
    char *m = receive_msg(fd);
    printf("RECV: %s\n", readable_status(m).c_str());
    if (strstr(m, "STATUS 3 ") != NULL) { // CLOSED; expect BETWEEN next
      test_recv(fd, "RECV: STATUS Between 1 4");
    }
    free(m);
  }
  test_recv(fd, "RECV: STATUS Between 2 4");
  test_recv(fd, "RECV: STATUS Between 3 4");
  test_recv(fd, "RECV: STATUS Opening 4 4");
  test_recv(fd, "RECV: STATUS Open 4 4");
  test_recv(fd, "RECV: STATUS Closing 4 4");
  test_recv(fd, "RECV: STATUS Closed 4 4");

  // Send the car the same floor to get it to open its doors again
  send_message(fd, "FLOOR 4");
  test_recv(fd, "RECV: STATUS Opening 4 4");
  test_recv(fd, "RECV: STATUS Open 4 4");
  test_recv(fd, "RECV: STATUS Closing 4 4");
  test_recv(fd, "RECV: STATUS Closed 4 4");

  // Send the car down to floor 2
  send_message(fd, "FLOOR 2");
  {
    msg("RECV: STATUS Closed 4 2 (or Between 4 2)");
    char *m = receive_msg(fd);
    printf("RECV: %s\n", readable_status(m).c_str());
    if (strstr(m, "STATUS 3 ") != NULL) { // CLOSED; expect BETWEEN next
      test_recv(fd, "RECV: STATUS Between 4 2");
    }
    free(m);
  }
  test_recv(fd, "RECV: STATUS Between 3 2");
  test_recv(fd, "RECV: STATUS Opening 2 2");
  test_recv(fd, "RECV: STATUS Open 2 2");
  test_recv(fd, "RECV: STATUS Closing 2 2");
  test_recv(fd, "RECV: STATUS Closed 2 2");

  close(fd);
  close(server_fd);

  cleanup(p);
  printf("\nTests completed.\n");
}

void test_recv(int fd, const char *t)
{
  char *m = receive_msg(fd);
  msg(t);
  printf("RECV: %s\n", readable_status(m).c_str());
  free(m);
}

std::string readable_status(const char *message)
{
  std::string readable = message;
  constexpr char prefix[] = "STATUS ";

  if (!readable.starts_with(prefix)) {
    return readable;
  }

  const size_t status_end = readable.find(' ', sizeof(prefix) - 1);
  if (status_end == std::string::npos) {
    return readable;
  }

  const std::string status_value = readable.substr(sizeof(prefix) - 1,
                                                    status_end - (sizeof(prefix) - 1));
  if (status_value.size() != 1 || status_value[0] < '0' || status_value[0] > '4') {
    return readable;
  }

  const Status status = static_cast<Status>(status_value[0] - '0');
  readable.replace(sizeof(prefix) - 1, status_value.size(), status_name(status));
  return readable;
}

void cleanup(pid_t p)
{
  kill(p, SIGINT);
  usleep(DELAY);
  shm_unlink("/carTest");
}

pid_t car(const char *name, const char *lowest_floor, const char *highest_floor, const char *delay)
{
  pid_t pid = fork();
  if (pid == 0) {
    execlp("./car", "./car", name, lowest_floor, highest_floor, delay, NULL);
  }

  return pid;
}

void server_init()
{
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(3000);
  a.sin_addr.s_addr = htonl(INADDR_ANY);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt_enable = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable));
  if (bind(server_fd, (const struct sockaddr *)&a, sizeof(a)) == -1) {
    perror("bind()");
    exit(1);
  }

  listen(server_fd, 10);
}

void *server(void *)
{
  return NULL;
}
