#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>

#define C_NUM 5            // maximum client num
#define N_LEN 30           // maximum nickname len
#define RM_LEN BUFSIZ - 50 // maximum receive message Len

char QUIT[5] = "QUIT";
char warning2Client[BUFSIZ] = "Sorry, there is none emtpy space in \'clientAddress\'.\nPlease reconnect after a while.\n";
char warning2Server[BUFSIZ] = "Warning(None Empty Space): There was a request, but ignored.\n";

pthread_mutex_t lock;

struct _data
{
  int idx;
  int res;
};
struct sendData
{
  char message[BUFSIZ - 50];
  int idx;
  int state; // state 1 is new user, state 2 is QUIT, state 3 is normal message
};

int s;                        // socket file descriptor + need mutex
int res[C_NUM];               // recv, send socket
char nicknames[C_NUM][N_LEN]; // nicknames
u_short port;                 // argv[1] is port input
struct sockaddr_in serverAddress;
struct sockaddr_in clientAddress[C_NUM];
socklen_t addressLength[C_NUM];

void usage(void)
{
  printf("Argument Format Error\n");
  printf("Example: ./server 8080\n");
}

void sig_handler(int signo)
{
  if (signo == SIGINT)
  {
    for (int i = 0; i < C_NUM; i++)
    {
      if (res[i] != 0)
        close(res[i]);
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

void bind()
{
  serverAddress.sin_family = AF_INET;                // IPv4
  serverAddress.sin_port = htons(port);              // host to network short (port)
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY); // host to network long (0.0.0.0) -> any IP address is available

  if (bind(s, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
  {
    perror("bind"); // error in binding
    exit(1);
  }
}

void listen(void)
{
  if (listen(s, 1) < 0)
  {
    perror("listen"); // error in listening
    exit(1);
  }
}

void clearQUIT(int idx)
{
  close(res[idx]);
  clientAddress[idx].sin_port = 0;
  res[idx] = 0;
  memset(nicknames[idx], 0, N_LEN);
}

void *broadcastThread(void *arg)
{
  sendData *_sendData = (sendData *)arg;
  char message[BUFSIZ];
  // state 1 is new user, state 2 is QUIT, state 3 is normal message
  if (_sendData->state == 1)
    sprintf(message, "%s is connected\n", nicknames[_sendData->idx]);
  else if (_sendData->state == 2)
  {
    sprintf(message, "%s is disconnected\n", nicknames[_sendData->idx]);
    clearQUIT(_sendData->idx);
  }
  else if (_sendData->state == 3)
    sprintf(message, "%s: %s", nicknames[_sendData->idx], _sendData->message);
  else
    perror("broadcast");

  printf("%s", message);
  for (int i = 0; i < C_NUM; i++)
  {
    if (res[i] == 0)
      continue;

    if (i != _sendData->idx)
    {
      if (send(res[i], message, BUFSIZ, 0) < 0)
        perror("send"); // error in sending
    }
  }
  free(_sendData);
  return 0;
}

void *recvThread(void *arg)
{
  pthread_t send_all;
  _data *data = (_data *)arg;
  int idx = data->idx;
  int terminate = 0;

  while (true)
  {
    sendData *_sendData = (sendData *)malloc(sizeof(sendData));
    _sendData->idx = data->idx;
    if (recv(data->res, _sendData->message, RM_LEN, 0) < 0)
    {
      perror("recv"); // error in receiving
    }

    // New User Connect -> Add Nickname
    if (strlen(nicknames[idx]) == 0)
    {
      memcpy(nicknames[idx], _sendData->message, N_LEN);
      _sendData->state = 1;
    }

    // check whether recv msg is equal to "QUIT" or not
    // if equal, then the result of memcmp is 0 .
    else if (!memcmp(QUIT, _sendData->message, 4))
    {
      // printf("%s is disconnected\n", nicknames[_sendData->idx]);
      terminate = 1;
      _sendData->state = 2;
    }

    else
      _sendData->state = 3;

    // send client's message to all clients
    pthread_create(&send_all, NULL, broadcastThread, _sendData);
    if (terminate)
    {
      free(data);
      break;
    }
  }

  return 0;
}

void noneEmptySpace(void)
{
  int nEmpty;
  struct sockaddr_in nEmptyAddress;
  socklen_t nEmptyaddressLength;
  nEmpty = accept(s, (struct sockaddr *)&nEmptyAddress, &nEmptyaddressLength);

  printf("%s", warning2Server);
  if (send(nEmpty, warning2Client, BUFSIZ, 0) < 0)
    perror("warning");

  close(nEmpty);
}

void acceptNstart(void)
{
  pthread_t recvThread_t[C_NUM];

  while (true)
  {
    int rest = -1;
    for (int i = 0; i < C_NUM; i++)
    {
      // if clientAddress is null
      if (clientAddress[i].sin_port == 0)
      {
        rest = i;
        break;
      }
    }

    if (rest == -1)
    {
      noneEmptySpace();
      continue;
    }

    res[rest] = accept(s, (struct sockaddr *)&clientAddress[rest], &addressLength[rest]);
    if (res[rest] < 0)
    {
      perror("accept"); // error in accepting
      continue;
    }

    // When the TCP connection is made, print the client's IP address and port number
    // convert IP in sockaddr_in structure to string format
    // network to host (port) in sockaddr_in structure
    printf("Connection from %s:%d\n", inet_ntoa(clientAddress[rest].sin_addr), ntohs(clientAddress[rest].sin_port));

    _data *data = (_data *)malloc(sizeof(_data));
    data->idx = rest;
    data->res = res[rest];
    pthread_create(&recvThread_t[rest], NULL, recvThread, data);
  }
}

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    usage();
    exit(1);
  }

  signal(SIGINT, sig_handler);

  // initialize the addressLength
  for (int i = 0; i < C_NUM; i++)
  {
    clientAddress[i].sin_port = 0; // u_short sin_port in sockaddr_in structure
    addressLength[i] = sizeof(clientAddress);
  }
  port = atoi(argv[1]); // argv[1] is port input

  make_socket();
  bind();
  listen();
  acceptNstart();
  puts("SEEE BALLLL RYUNNN AAA");
  close(s); // close the socket file descriptor
  return 0;
}