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

const char QUIT[SM_LEN] = "QUIT";

int s;                            // socket file descriptor
char sendBuf[SM_LEN];             // send buffer with BUFSIZ
char *serverIP;                   // argv[1] is Server IP input
u_short port;                     // argv[2] is port input
char *nickname;                   // client's nickname
struct sockaddr_in clientAddress; // client's address
int client_len;

pthread_t send_thread; // send thread
pthread_t recv_thread; // receive thread

int isRecvTerminated = 0;
int isSendTerminated = 0;
int isTerminated = 0;

void usage(void)
{
  printf("Argument Format Error\n");
  printf("Example: ./client \"192.168.1.105\" 8080 User1\n");
}

// when SIGINT signal occurs,
// send "QUIT" message to server and close the connection
void sig_handler(int signo)
{
  if (signo == SIGINT)
  {
    if (send(s, QUIT, SM_LEN, 0) < 0)
      perror("QUIT"); // error in QUIT
    close(s);
    exit(1);
  }
}

// make socket
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

  printf("Connection Success\n");
  if (send(s, nickname, N_LEN, 0) < 0) // send client's nickname to server
  {
    perror("send nickname"); // error in sending nickname
  }
}

// receive message and print it in client's screen Thread
void *recvThread(void *arg)
{
  char message[BUFSIZ];
  while (true)
  {
    memset(message, 0x00, BUFSIZ); // clear the buffer
    if (recv(s, message, BUFSIZ, 0) < 0)
      perror("recv");

    if (!memcmp(QUIT, message, 4) && (strlen(message) == 5 || strlen(message) == 4))
    {
      // terminate the send thread when the receive thread terminated.
      isTerminated = 1;
      pthread_cancel(send_thread);
      break;
    }
    printf("%s", message);
  }
  return 0;
}

// receive send message from stdin, and send it to server
// if the send message is equal to "QUIT", then this thread would be terminated
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
    if (!memcmp(QUIT, sendBuf, 4) && strlen(sendBuf) == 5)
    {
      printf("Disconnected\n");
      isTerminated = 1;
      // terminate the receive thread when the send thread terminated.
      pthread_cancel(recv_thread);
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
  nickname = argv[3];   // argv[3] is client's nickname
  client_len = sizeof(clientAddress);

  int result;

  make_socket();
  connectNsendName();

  pthread_create(&recv_thread, NULL, recvThread, NULL); // start receive tread
  pthread_create(&send_thread, NULL, sendThread, NULL); // start send thread

  // clear the memory when the threads terminated
  // not to recv or send bad file descriptor
  pthread_detach(recv_thread);
  pthread_detach(send_thread);

  // wait until the receive and send thread terminated.
  while (!isTerminated)
    ;

  close(s); // close the socket file descriptor
  return 0;
}