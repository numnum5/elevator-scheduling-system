#include "shared.h"
#include <sys/wait.h>

// Tester for controller (single car, very basic tests)

constexpr useconds_t DELAY = 50000; // 50ms

pid_t controller(void);
int connect_to_controller(void);
void test_call(const char *, const char *);
void test_recv(int, const char *);
void cleanup(pid_t);

int main()
{
  const pid_t p = controller();
  usleep(DELAY);


  // Attempt to call an elevator before any cars register
  test_call("CALL 1 2", "UNAVAILABLE");

  // Register a car that can take floors B1 to 4
  int alpha = connect_to_controller();
  // Send initialisation message and first status message
  send_message(alpha, "CAR Alpha B1 4");
  send_message(alpha, "STATUS 3 B1 B1");
  usleep(DELAY);

  // Call an elevator. Alpha should be dispatched and told to go to 1
  test_call("CALL 1 2", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 1");

  // Send status updates telling the controller that the car is moving
  send_message(alpha, "STATUS 4 B1 1");
  send_message(alpha, "STATUS 0 1 1");
  // Now that Alpha has arrived at floor 1 it should be told to go to floor 2
  test_recv(alpha, "RECV: FLOOR 2");

  send_message(alpha, "STATUS 1 1 2");
  send_message(alpha, "STATUS 2 1 2");
  send_message(alpha, "STATUS 4 1 2");
  send_message(alpha, "STATUS 0 2 2");
  send_message(alpha, "STATUS 1 2 2");
  send_message(alpha, "STATUS 2 2 2");
  send_message(alpha, "STATUS 3 2 2");
  usleep(DELAY);
  
  // Call an elevator from a floor Alpha does not service
  test_call("CALL 5 1", "UNAVAILABLE");
  // Call an elevator to a floor Alpha does not service
  test_call("CALL B2 3", "UNAVAILABLE");
  
  // Call from floor 2 to floor 1. Alpha can service this one
  test_call("CALL 2 1", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 2");
  send_message(alpha, "STATUS 0 2 2");
  test_recv(alpha, "RECV: FLOOR 1");
  send_message(alpha, "STATUS 1 2 1");
  send_message(alpha, "STATUS 2 2 1");
  send_message(alpha, "STATUS 4 2 1");
  send_message(alpha, "STATUS 0 1 1");
  send_message(alpha, "STATUS 1 1 1");
  send_message(alpha, "STATUS 2 1 1");
  send_message(alpha, "STATUS 3 1 1");

  cleanup(p);

  close(alpha);

  printf("\nTests completed.\n");
}

void test_call(const char *sendmsg, const char *expectedreply)
{
  int fd = connect_to_controller();
  send_message(fd, sendmsg);
  char *reply = receive_msg(fd);
  msg(expectedreply);
  printf("%s\n", reply);
  free(reply);
  close(fd);
}

void test_recv(int fd, const char *t)
{
  char *m = receive_msg(fd);
  msg(t);
  printf("RECV: %s\n", m);
  free(m);
}

int connect_to_controller(void)
{
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
  {
    perror("socket()");
    exit(1);
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(3000);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == -1)
  {
    perror("connect()");
    close(fd);
    exit(1);
  }
  return fd;
}

void cleanup(pid_t p)
{
  // Terminate with SIGINT to allow server to clean up
  if (kill(p, SIGINT) == -1) {
    perror("kill()");
  }

  if (waitpid(p, nullptr, 0) == -1) {
    perror("waitpid()");
  }
}

pid_t controller(void)
{
  const pid_t pid = fork();
  if (pid == -1) {
    perror("fork()");
    exit(1);
  }

  if (pid == 0) {
    execlp("./controller", "./controller", NULL);
    perror("execlp()");
    _exit(127);
  }

  return pid;
}
