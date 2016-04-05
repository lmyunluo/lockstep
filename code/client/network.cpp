#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "lib/def.h"
#include "lib/min_max.h"
#include "lib/assert.h"
#include "common/network.h"
#include "network.h"

network_buffer ReceiveBuffer;

enum errno_code {
  errno_code_interrupted_system_call = 4,
  errno_code_in_progress = 36
};

enum main_state {
  main_state_inactive,
  main_state_connecting,
  main_state_connected,
  main_state_shutting_down,
  main_state_stopped
};

static int SocketFD;
static main_state MainState;
static int WakeReadFD;
static int WakeWriteFD;
static int FDMax;
static bool ShutdownRequested;

static void RequestWake() {
  ui8 X = 1;
  write(WakeWriteFD, &X, 1);
}

void InitReceiveBuffer() {
  size_t Capacity = 1024*100;
  void *Data = malloc(Capacity);
  InitNetworkBuffer(&ReceiveBuffer, Data, Capacity);
}

void InitNetwork2() {
  InitReceiveBuffer();

  ShutdownRequested = false;

  SocketFD = socket(PF_INET, SOCK_STREAM, 0);
  Assert(SocketFD != -1);
  int Result = fcntl(SocketFD, F_SETFL, O_NONBLOCK);
  Assert(Result != -1);

  {
    int FDs[2];
    pipe(FDs);
    WakeReadFD = FDs[0];
    WakeWriteFD = FDs[1];
  }

  FDMax = MaxInt(WakeReadFD, SocketFD);

  MainState = main_state_inactive;
}

void Connect() {
  struct sockaddr_in Address;
  memset(&Address, 0, sizeof(Address));
  Address.sin_len = sizeof(Address);
  Address.sin_family = AF_INET;
  Address.sin_port = htons(4321);
  Address.sin_addr.s_addr = 0;

  int ConnectResult = connect(SocketFD, (struct sockaddr *)&Address, sizeof(Address));
  Assert(ConnectResult != -1 || errno == errno_code_in_progress);

  MainState = main_state_connecting;
}

void* RunNetwork(void *Data) {
  Connect();

  while(MainState != main_state_stopped) {
    fd_set FDSet;
    FD_ZERO(&FDSet);
    FD_SET(SocketFD, &FDSet);
    FD_SET(WakeReadFD, &FDSet);

    int SelectResult = select(FDMax+1, &FDSet, NULL, NULL, NULL);
    Assert(SelectResult != -1);

    if(FD_ISSET(WakeReadFD, &FDSet)) {
      ui8 X;
      int Result = read(WakeReadFD, &X, 1);
      Assert(Result != -1);

      if(ShutdownRequested && MainState != main_state_shutting_down) {
        printf("Shutting down...\n");
        int Result = shutdown(SocketFD, SHUT_RDWR);
        Assert(Result == 0);
        MainState = main_state_shutting_down;
        continue;
      }
    }

    if(MainState == main_state_connected || MainState == main_state_shutting_down) {
      if(FD_ISSET(SocketFD, &FDSet)) {
        ssize_t Result = NetworkReceive(SocketFD, &ReceiveBuffer);
        if(Result == 0) {
          printf("Disconnected.\n");
          MainState = main_state_stopped;
        }
        else {
          printf("Got something of length: %zd, as char %u\n", (int)Result, *(ui8*)ReceiveBuffer.Data);
        }
      }
    }
    else if(MainState == main_state_connecting) {
      int OptionValue;
      socklen_t OptionLength = sizeof(OptionValue);
      getsockopt(SocketFD, SOL_SOCKET, SO_ERROR, &OptionValue, &OptionLength);
      if(OptionValue == 0) {
        MainState = main_state_connected;
        printf("Connected.\n");
      }
      else {
        printf("Connection failed.\n");
        MainState = main_state_stopped;
      }
    }
  }

  printf("Network thread done.\n");

  return NULL;
}

void ShutdownNetwork() {
  ShutdownRequested = true;
  RequestWake();
}

void TerminateNetwork2() {
  int Result = close(SocketFD);
  Assert(Result == 0);

  Result = close(WakeReadFD);
  Assert(Result == 0);
  Result = close(WakeWriteFD);
  Assert(Result == 0);

  free(ReceiveBuffer.Data);
  TerminateNetworkBuffer(&ReceiveBuffer);
  MainState = main_state_inactive;
}
