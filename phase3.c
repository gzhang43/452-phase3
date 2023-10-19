#include <stdio.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase3_usermode.h"

int(*func)(char*);

typedef struct PCB {
    char* name;
    int (*startFunc)(char*);
    char* arg;
    int pid;
    int priority;
    int status;
} PCB;

void kernSpawn(USLOSS_Sysargs *arg);
void kernWait(USLOSS_Sysargs *arg);
void kernTerminate(USLOSS_Sysargs *arg);
void kernGetTimeOfDay(USLOSS_Sysargs* arg);
void kernCPUTime(USLOSS_Sysargs* arg);
void kernGetPID(USLOSS_Sysargs* arg);

void phase3_init(void) {
    systemCallVec[3] = kernSpawn;
    systemCallVec[4] = kernWait;
    systemCallVec[5] = kernTerminate;
    systemCallVec[20] = kernGetTimeOfDay;
    systemCallVec[21] = kernCPUTime;
    systemCallVec[22] = kernGetPID;
}

void phase3_start_service_processes(void) {

}

int trampolineFunc(char *arg) {
    int result = USLOSS_PsrSet(USLOSS_PsrGet() & ~1); // enable user mode
    if (result == 1) {
        USLOSS_Console("Error: invalid PSR value for set.\n");
        USLOSS_Halt(1);
    }
    int status = func(arg);
    Terminate(status);
}

void kernSpawn(USLOSS_Sysargs *arg) {
    func = (int(*)(char*))arg->arg1;
    int stackSize = (int)(long)arg->arg3;
    int priority = (int)(long)arg->arg4;

    int ret = fork1(arg->arg5, trampolineFunc, arg->arg2, stackSize, priority);

    arg->arg1 = (void*)(long)ret;
    arg->arg4 = (void*)(long)0;
}

void kernWait(USLOSS_Sysargs *arg) {
    int status;
    int ret = join(&status);
    
    if (ret == -2) {
        arg->arg4 = (void*)(long)-2;
    }
    else {
        arg->arg4 = (void*)(long)0;
        arg->arg1 = (void*)(long)ret;
        arg->arg2 = (void*)(long)status;
    }
}

void kernTerminate(USLOSS_Sysargs *arg) {
    int status = (int)(long)arg->arg1;
    int joinStatus;
    
    int ret = join(&joinStatus);
    while (ret != -2) {
        ret = join(&joinStatus);
    }
    quit(status);
}

void kernGetTimeOfDay(USLOSS_Sysargs* arg) {
    arg->arg1 = (void*)(long)currentTime();
}

void kernCPUTime(USLOSS_Sysargs* arg) {
    arg->arg1 = (void*)(long)readtime();
}

void kernGetPID(USLOSS_Sysargs* arg) {
    arg->arg1 = (void*)(long)getpid();
}
