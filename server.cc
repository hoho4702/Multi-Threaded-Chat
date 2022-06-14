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

const char QUIT[5] = "QUIT";
const char warning2Server[BUFSIZ] = "Warning(None Empty Space): There was a request, but ignored.\n";
const char warning2Client[BUFSIZ] = "Sorry, there is none emtpy space in Server's \'clientAddress\'.\nPlease reconnect after a while.\n";

pthread_mutex_t mutex_lock;
pthread_t recvThread_t[C_NUM];

// struct having informations needed in the receive thread
struct _data
{
  int idx; // index for res
  int res; // res(socket) itself
};
// struct having informations needed in the sending Data
struct sendData
{
  char message[BUFSIZ - 50]; // sending message
  int idx;                   // index for res(socket), which can distinct who the client is
  int state;                 // state 1 means new user, state 2 means QUIT, state 3 means normal message
};

// need mutex
int s;                                   // socket file descriptor
int res[C_NUM];                          // recv, send socket
char nicknames[C_NUM][N_LEN];            // nicknames
struct sockaddr_in clientAddress[C_NUM]; // clients' addresses. clientAddress[i] == 0 means res[i] is not connected yet (empty space).

// do not need mutex
u_short port;                     // argv[1] is port input
struct sockaddr_in serverAddress; // server's address
socklen_t addressLength[C_NUM];

void usage(void)
{
  printf("Argument Format Error\n");
  printf("Example: ./server 8080\n");
}

// when SIGINT signal occurs,
// send Shutdown err message to clients and close the connections with them
void sig_handler(int signo)
{
  if (signo == SIGINT)
  {
    pthread_mutex_lock(&mutex_lock); // mutex for res
    char server_down_errMsg[BUFSIZ] = "Connection Closed: Server Shutdowned\n";
    for (int i = 0; i < C_NUM; i++)
    {
      if (res[i] != 0)
      {
        if (send(res[i], server_down_errMsg, BUFSIZ, 0) < 0) // notify Client that Server Shutdowned
          perror("server shutdown");                         // error in server shutdown
        if (send(res[i], QUIT, BUFSIZ, 0) < 0)               // notify client to close the connection
          perror("warning");
        pthread_detach(recvThread_t[i]); // clear the memory of recv thread
        pthread_cancel(recvThread_t[i]); // terminate recv thread
        close(res[i]);
      }
    }

    printf("Server Shutdowned.\n");

    // terminate the recv thread and clear the memory

    pthread_mutex_unlock(&mutex_lock);
    close(s);
    exit(1);
  }
}

void init(int port_input)
{
  signal(SIGINT, sig_handler);
  pthread_mutex_init(&mutex_lock, NULL); // initialize the mutex_lock

  // initialize the addressLength
  for (int i = 0; i < C_NUM; i++)
  {
    clientAddress[i].sin_port = 0; // u_short sin_port in sockaddr_in structure
    addressLength[i] = sizeof(clientAddress);
  }
  port = port_input; // argv[1] is port input
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

// bind to the port received in the command line
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

// server listening
void listen(void)
{
  if (listen(s, 1) < 0)
  {
    perror("listen"); // error in listening
    exit(1);
  }
}

// clear all buffer related to the client which sent "QUIT" message
void clearQUIT(int idx)
{
  pthread_mutex_lock(&mutex_lock);  // mutex for res, clientAddress, and nickname
  close(res[idx]);                  // close the socket
  clientAddress[idx].sin_port = 0;  // clear the clientAddress's port to reuse it
  res[idx] = 0;                     // clear the res(socket) to reuse it
  memset(nicknames[idx], 0, N_LEN); // clear the nickname to reuse it
  pthread_mutex_unlock(&mutex_lock);
}

// broadcast Thread which send the message received by certain client to all clients without the sending client itself
// arg have informations include the message, who sent it, and the message's category.
void *broadcastThread(void *arg)
{
  sendData *_sendData = (sendData *)arg;
  char message[BUFSIZ];

  pthread_mutex_lock(&mutex_lock); // mutex for nickname
  // state 1 means new user, state 2 means QUIT, state 3 means normal message
  if (_sendData->state == 1) // new user connected, so the message contains the client's nickname
    sprintf(message, "%s is connected\n", nicknames[_sendData->idx]);
  else if (_sendData->state == 2) // QUIT request received, so execute the clearQUIT() function
  {
    sprintf(message, "%s is disconnected\n", nicknames[_sendData->idx]);
    pthread_mutex_unlock(&mutex_lock); // unlock for mutex using in clearQUIT()
    clearQUIT(_sendData->idx);
    pthread_mutex_lock(&mutex_lock);
  }
  else if (_sendData->state == 3) // normal message received
    sprintf(message, "%s: %s", nicknames[_sendData->idx], _sendData->message);
  else
  {
    perror("broadcast");
    free(_sendData);
    return 0;
  }
  printf("%s", message);
  pthread_mutex_unlock(&mutex_lock);

  pthread_mutex_lock(&mutex_lock); // mutex for res
  for (int i = 0; i < C_NUM; i++)
  {
    if (res[i] == 0)
      continue;

    if (i != _sendData->idx || _sendData->state == 1) // except the client who sent the message
    {
      if (send(res[i], message, BUFSIZ, 0) < 0)
        perror("send"); // error in sending
    }
  }
  pthread_mutex_unlock(&mutex_lock);
  free(_sendData); // free the heap data
  return 0;
}

void *recvThread(void *arg)
{
  pthread_t send_all;
  _data *data = (_data *)arg;
  int idx = data->idx;
  int isTerminate = 0;

  while (true)
  {
    sendData *_sendData = (sendData *)malloc(sizeof(sendData));
    _sendData->idx = data->idx;
    if (recv(data->res, _sendData->message, RM_LEN, 0) < 0)
    {
      perror("recv"); // error in receiving
    }
    if (strlen(_sendData->message) == 0)
    {
      free(_sendData);
      continue;
    }

    pthread_mutex_lock(&mutex_lock); // mutex for nickname
    // New User Connect -> Add Nickname
    if (strlen(nicknames[idx]) == 0)
    {
      memcpy(nicknames[idx], _sendData->message, N_LEN);
      _sendData->state = 1;
    }
    // check whether recv msg is equal to "QUIT" or not
    // if equal, then the result of memcmp is 0 .
    else if (!memcmp(QUIT, _sendData->message, 4) && (strlen(_sendData->message) == 4 || strlen(_sendData->message) == 5))
    {
      isTerminate = 1;
      _sendData->state = 2;
    }

    else
      _sendData->state = 3;
    pthread_mutex_unlock(&mutex_lock);

    // send client's message to all clients
    pthread_create(&send_all, NULL, broadcastThread, _sendData);
    if (isTerminate)
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

  // warning to server and client, then close by sending QUIT
  printf("%s", warning2Server);
  if (send(nEmpty, warning2Client, BUFSIZ, 0) < 0)
    perror("warning");

  if (send(nEmpty, QUIT, BUFSIZ, 0) < 0)
    perror("warning");
  close(nEmpty);
}

void acceptNstart(void)
{
  int r_tmp; // temporal value storing the value res[rest]

  while (true)
  {
    int rest = -1;
    pthread_mutex_lock(&mutex_lock); // mutex for clientAddress
    for (int i = 0; i < C_NUM; i++)
    {
      // if clientAddress is null
      if (clientAddress[i].sin_port == 0)
      {
        rest = i;
        break;
      }
    }
    pthread_mutex_unlock(&mutex_lock);

    if (rest == -1)
    {
      noneEmptySpace();
      continue;
    }

    r_tmp = accept(s, (struct sockaddr *)&clientAddress[rest], &addressLength[rest]);
    if (r_tmp < 0)
    {
      perror("accept"); // error in accepting
      continue;
    }

    pthread_mutex_lock(&mutex_lock); // mutex for res
    res[rest] = r_tmp;

    // When the TCP connection is made, print the client's IP address and port number
    // convert IP in sockaddr_in structure to string format
    // network to host (port) in sockaddr_in structure
    printf("Connection from %s:%d\n", inet_ntoa(clientAddress[rest].sin_addr), ntohs(clientAddress[rest].sin_port));

    _data *data = (_data *)malloc(sizeof(_data));
    data->idx = rest;
    data->res = res[rest];
    pthread_mutex_unlock(&mutex_lock);
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

  init(atoi(argv[1]));
  make_socket();
  bind();
  listen();
  acceptNstart();

  close(s); // close the socket file descriptor
  return 0;
}