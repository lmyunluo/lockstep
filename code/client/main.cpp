#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include "lib/assert.h"
#include "common/shared.h"
#include "common/memory.h"
#include "shared.h"
#include "network.h"

static bool DisconnectRequested;

struct main_state {
  void *Memory;
  linear_allocator Allocator;
  pthread_t NetworkThread;
};

static void HandleSigint(int signum) {
  DisconnectRequested = true;
}

void InitMemory(main_state *State) {
  memsize MemorySize = 1024*200;
  State->Memory = malloc(MemorySize);
  InitLinearAllocator(&State->Allocator, State->Memory, MemorySize);
}

void TerminateMemory(main_state *State) {
  TerminateLinearAllocator(&State->Allocator);
  free(State->Memory);
  State->Memory = NULL;
}

int main() {
  ClientRunning = true;
  signal(SIGINT, HandleSigint);

  main_state MainState;
  InitMemory(&MainState);
  InitNetwork2();
  {
    int Result = pthread_create(&MainState.NetworkThread, 0, RunNetwork, 0);
    Assert(Result == 0);
  }

  // TODO: Implement "nice" TCP shutdown instead of just using close()
  while(ClientRunning) {
    if(DisconnectRequested) {
      printf("Requesting network shutdown...\n");
      ShutdownNetwork();
      ClientRunning = false;
    }
  }

  {
    printf("Waiting for thread join...\n");
    int Result = pthread_join(MainState.NetworkThread, 0);
    Assert(Result == 0);
  }

  TerminateNetwork2();
  TerminateMemory(&MainState);
  printf("Gracefully terminated.\n");
  return 0;
}
