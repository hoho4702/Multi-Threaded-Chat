// Chat Client
#include <sys/stat.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define C_NUM 5            // maximum client num
#define N_LEN 30           // maximum nickname len
#define SM_LEN BUFSIZ - 50 // maximum send message len

char QUIT[] = "QUIT";

pthread_mutex_t lock;

int s;                // socket file descriptor
char sendBuf[SM_LEN]; // send buffer with BUFSIZ
char *serverIP;       // argv[1] is Server IP input
u_short port;         // argv[2] is port input
char *nickname;
struct sockaddr_in clientAddress;
int client_len;

void usage(void)
{
  printf("Argument Format Error\n");
  printf("Example: ./client \"192.168.1.105\" 8080 User1\n");
}

void sig_handler(int signo)
{
  if (signo == SIGINT)
  {
    char quit_message[SM_LEN] = "QUIT";
    // sprintf(sendBuf, "QUIT");////
    if (send(s, quit_message, SM_LEN, 0) < 0)
    {
      perror("QUIT"); // error in sending
    }
    close(s);
    exit(1);
  }
}

void make_socket(void)
{
  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP); // SOCK_STREAM, IPPROTO_TCP come together in TCP
  if (s < 0)
  {
    perror("socket"); // error in making socket
    exit(1);
  }
}

void connectNsendName(void)
{
  clientAddress.sin_family = AF_INET;                  // IPv4
  clientAddress.sin_port = htons(port);                // host to network short (port)
  clientAddress.sin_addr.s_addr = inet_addr(serverIP); // host to network long (Server IP)

  if (connect(s, (struct sockaddr *)&clientAddress, client_len) < 0)
  {
    perror("connect"); // error in connecting
    close(s);
    exit(1);
  }

  if (send(s, nickname, N_LEN, 0) < 0)
  {
    perror("send nickname"); // error in sending
  }
}

void *recvThread(void *arg)
{
  char message[BUFSIZ];
  while (true)
  {
    memset(message, 0x00, BUFSIZ);
    if (recv(s, message, BUFSIZ, 0) < 0)
    {
      perror("recv");
    }
    printf("%s", message);
  }
  return 0;
}

void *sendThread(void *arg)
{
  while (true)
  {
    memset(sendBuf, 0x00, SM_LEN);
    fgets(sendBuf, SM_LEN, stdin);
    if (send(s, sendBuf, SM_LEN, 0) < 0)
    {
      perror("send"); // error in sending
    }

    // check whether send msg is equal to "QUIT" or not
    // if equal, then the result of memcmp is 0 .
    if (!memcmp(QUIT, sendBuf, 4))
    {
      printf("Disconnected\n");
      break;
    }
  }
  return 0;
}

int main(int argc, char **argv)
{
  if (argc != 4)
  {
    usage();
    exit(1);
  }

  signal(SIGINT, sig_handler);

  serverIP = argv[1];   // argv[1] is Server IP input
  port = atoi(argv[2]); // argv[2] is port input
  nickname = argv[3];
  client_len = sizeof(clientAddress);

  pthread_t send_thread;
  pthread_t recv_thread;
  int result;

  make_socket();
  connectNsendName();

  pthread_create(&recv_thread, NULL, recvThread, NULL);
  pthread_create(&send_thread, NULL, sendThread, NULL);

  // pthread_join(send_thread, (void *)&result);
  pthread_join(send_thread, (void **)&result);

  close(s); // close the socket file descriptor
  return 0;
}