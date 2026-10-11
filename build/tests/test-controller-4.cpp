#include "shared.h"

// Tester for controller (single car, multiple stops, test for proper routing)

#define DELAY 50000 // 50ms
#define MILLISECOND 1000 // 1ms

pid_t controller(void);
int connect_to_controller(void);
void test_call(const char *, const char *);
void test_recv(int, const char *);
void cleanup(pid_t);

int main()
{
  pid_t p;
  p = controller();
  usleep(DELAY);

  int alpha = connect_to_controller();
  send_message(alpha, "CAR Alpha 1 50");
  send_message(alpha, "STATUS 3 1 1");
  usleep(DELAY);
  test_call("CALL 3 6", "CAR Alpha");
  // Queue should be: 3 6
  test_recv(alpha, "RECV: FLOOR 3");
  send_message(alpha, "STATUS 4 1 3");
  send_message(alpha, "STATUS 4 2 3");
  usleep(DELAY);

  test_call("CALL 7 4", "CAR Alpha");
  // These should be enqueued at the end, because there is no point
  // in stopping at 4 until picking up the passenger at 7
  // So the queue should be: 3 6 7 4

  send_message(alpha, "STATUS 0 3 3");
  test_recv(alpha, "RECV: FLOOR 6");
  send_message(alpha, "STATUS 1 3 6");
  send_message(alpha, "STATUS 2 3 6");
  send_message(alpha, "STATUS 4 3 6");
  send_message(alpha, "STATUS 4 4 6");
  usleep(DELAY);
  test_call("CALL 8 4", "CAR Alpha");
  // To prevent extra changes in direction, 8 should be added before
  // the car turns around, and 4 is in the queue already
  // Queue should be: 6 8 7 4

  send_message(alpha, "STATUS 4 5 6");
  send_message(alpha, "STATUS 0 6 6");
  test_recv(alpha, "RECV: FLOOR 8");
  send_message(alpha, "STATUS 1 6 8");
  send_message(alpha, "STATUS 2 6 8");
  send_message(alpha, "STATUS 4 6 8");
  send_message(alpha, "STATUS 4 7 8");
  send_message(alpha, "STATUS 0 8 8");
  test_recv(alpha, "RECV: FLOOR 7");
  send_message(alpha, "STATUS 1 8 7");
  send_message(alpha, "STATUS 2 8 7");
  usleep(DELAY);
  test_call("CALL 6 5", "CAR Alpha");
  // This two extra stops are on the way, so they should just be
  // added into the queue before 4
  // Queue should be: 7 6 5 4

  send_message(alpha, "STATUS 4 8 7");
  send_message(alpha, "STATUS 0 7 7");
  test_recv(alpha, "RECV: FLOOR 6");
  send_message(alpha, "STATUS 1 7 6");
  send_message(alpha, "STATUS 2 7 6");
  send_message(alpha, "STATUS 4 7 6");
  send_message(alpha, "STATUS 0 6 6");
  test_recv(alpha, "RECV: FLOOR 5");
  send_message(alpha, "STATUS 1 6 5");
  send_message(alpha, "STATUS 2 6 5");
  send_message(alpha, "STATUS 4 6 5");
  send_message(alpha, "STATUS 0 5 5");
  test_recv(alpha, "RECV: FLOOR 4");
  send_message(alpha, "STATUS 1 5 4");
  send_message(alpha, "STATUS 2 5 4");
  send_message(alpha, "STATUS 4 5 4");
  send_message(alpha, "STATUS 0 4 4");
  send_message(alpha, "STATUS 1 4 4");
  send_message(alpha, "STATUS 2 4 4");
  send_message(alpha, "STATUS 3 4 4");
  usleep(DELAY);
  test_call("CALL 4 2", "CAR Alpha");
  // Car is already on 4, so it should open its doors again
  // Queue should be: 4 2.
  test_recv(alpha, "RECV: FLOOR 4");
  send_message(alpha, "STATUS 0 4 4");
  test_recv(alpha, "RECV: FLOOR 2");
  send_message(alpha, "STATUS 1 4 2");
  send_message(alpha, "STATUS 2 4 2");
  send_message(alpha, "STATUS 4 4 2");
  send_message(alpha, "STATUS 4 3 2");
  send_message(alpha, "STATUS 0 2 2");
  send_message(alpha, "STATUS 1 2 2");
  send_message(alpha, "STATUS 2 2 2");
  send_message(alpha, "STATUS 3 2 2");

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
  msg(t);
  char *m = receive_msg(fd);
  printf("RECV: %s\n", m);
  free(m);
}

int connect_to_controller(void)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(3000);
  sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (const struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1)
  {
    perror("connect()");
    exit(1);
  }
  return fd;
}

void cleanup(pid_t p)
{
  // Terminate with SIGINT to allow server to clean up
  kill(p, SIGINT);
}

pid_t controller(void)
{
  pid_t pid = fork();
  if (pid == 0) {
    execlp("./controller", "./controller", NULL);
  }

  return pid;
}
