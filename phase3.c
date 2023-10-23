/*
Assignment: Phase 3
Group: Grace Zhang and Ellie Martin
Course: CSC 452 (Operating Systems)
Instructors: Russell Lewis and Ben Dicken
Due Date: 10/25/23

Description: Code for Phase 3 of our operating systems kernel that implements
the syscalls for Spawn, Wait, Terminate, SemCreate, SemP, SemV, GetTimeOfDay,
CPUTime, and GetPid. Phase 3 initializes the syscall vector with function pointers
to our implementations and uses mailboxes to block and unblock processes and 
acquire mutexes.  

To compile with testcases, run the Makefile. 
*/

#include <stdio.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase3_usermode.h"

typedef struct PCB {
    int (*startFunc)(char*);
    char* arg;
    int pid;
    int mboxNum;
    int filled;
    struct PCB* nextBlockedProc;
} PCB;

void kernSpawn(USLOSS_Sysargs *arg);
void kernWait(USLOSS_Sysargs *arg);
void kernTerminate(USLOSS_Sysargs *arg);
void kernSemCreate(USLOSS_Sysargs* arg);
void kernGetTimeOfDay(USLOSS_Sysargs* arg);
void kernCPUTime(USLOSS_Sysargs* arg);
void kernGetPID(USLOSS_Sysargs* arg);
void kernSemP(USLOSS_Sysargs* arg);
void kernSemV(USLOSS_Sysargs* arg);

struct PCB processTable3[MAXPROC+1];
int semaphoresList[MAXSEMS]; // array containing semaphores
struct PCB* semaphoreBlockedProc[MAXSEMS]; // array of lists of processes blocked 
                                           // on each semaphore
int numberOfSems;
int mboxIdInts; // id of mailbox for enabling/disabling interrupts

/*
Function to initialize data structures required in Phase 3. Initializes the 
system call vector with the system calls implemented in this file, in addition to
the mailbox used to implement mutexes for functions.
*/
void phase3_init(void) {
    systemCallVec[3] = kernSpawn;
    systemCallVec[4] = kernWait;
    systemCallVec[5] = kernTerminate;
    systemCallVec[16] = kernSemCreate;
    systemCallVec[17] = kernSemP;
    systemCallVec[18] = kernSemV;
    systemCallVec[20] = kernGetTimeOfDay;
    systemCallVec[21] = kernCPUTime;
    systemCallVec[22] = kernGetPID;

    numberOfSems = 0;
    mboxIdInts = MboxCreate(1, 0);
 
    for (int i = 0; i < MAXPROC; i++) {
        processTable3[i].filled = 0;
    }
}

/*
Function to run service processes for Phase 3.
*/
void phase3_start_service_processes(void) {

}

/*
Has process acquire a lock by sending a message to the global one-slot mailbox.
*/
void acquireLock() {
    MboxSend(mboxIdInts, NULL, 0);
}

/*
Has process release its lock by receiving a message from the global one-slot 
mailbox.
*/
void releaseLock() {
    MboxCondRecv(mboxIdInts, NULL, 0); // Cond so it does not block if no lock
}

/*
Trampoline function to run the user function specified by Spawn. It stores the info
of the child process in this phase's shadow process table if this hasn't been done
by kernSpawn, and then runs the function in user mode.

Parameters:
    arg - the argument to be supplied to the function to run

Returns: None 
*/
int trampolineFunc(char *arg) {
    int pid = getpid();
    struct PCB* child = &processTable3[pid % MAXPROC];
    if (child->filled == 0) {
        child->pid = pid;
        child->filled = 1;
        child->mboxNum = MboxCreate(1, 0); // create one-slot mailbox for blocking

        MboxRecv(child->mboxNum, NULL, 0); // block this process
    }

    int result = USLOSS_PsrSet(USLOSS_PsrGet() & ~1); // enable user mode
    if (result == 1) {
        USLOSS_Console("Error: invalid PSR value for set.\n");
        USLOSS_Halt(1);
    }
    int status = processTable3[pid % MAXPROC].startFunc(arg);
    Terminate(status);
}

/*
Implementation of the syscall for Spawn that creates a new process and runs it in
user mode. 

Parameters:
    arg.arg1 - address of the user-main function func
    arg.arg2 - parameter arg to pass to the user-main function
    arg.arg3 - the stack size for the process
    arg.arg4 - the priority of the process
    arg.arg5 - a pointer to the character string with the new process's name

Returns:
    arg.arg1 - the PID of the newly created process, or -1 if it could not be
               created
    arg.arg4 - 01 if illegal values were given as input; 0 otherwise
*/
void kernSpawn(USLOSS_Sysargs *arg) {
    acquireLock(); // disable interrupts
    int (*func)(char*) = (int(*)(char*))arg->arg1;
    int stackSize = (int)(long)arg->arg3;
    int priority = (int)(long)arg->arg4;

    releaseLock();
    int ret = fork1(arg->arg5, trampolineFunc, arg->arg2, stackSize, priority);
    acquireLock();

    struct PCB* child = &processTable3[ret % MAXPROC];
    if (child->filled == 0) {
        child->pid = ret;
        child->startFunc = func;
        child->filled = 1;
        child->mboxNum = MboxCreate(1, 0); // create 0-slot mailbox for blocking
    }
    else {
        child->startFunc = func;
        releaseLock();
        MboxSend(child->mboxNum, NULL, 0);
        acquireLock();
    }

    arg->arg1 = (void*)(long)ret;
    arg->arg4 = (void*)(long)0;
    releaseLock();
}

/*
System call that calls join() and returns the PID and status that join() provides.

Parameters: USLOSS_Sysargs* arg is provided to store return values

Returns:
    arg.arg1 - the PID of the cleaned up process
    arg.arg2 - the status of the cleaned up process
    arg.arg4 - -2 if no children; 0 otherwise
*/
void kernWait(USLOSS_Sysargs *arg) {
    int status;
    int ret = join(&status);
    acquireLock();
    
    if (ret == -2) {
        arg->arg4 = (void*)(long)-2;
    }
    else {
        arg->arg4 = (void*)(long)0;
        arg->arg1 = (void*)(long)ret;
        arg->arg2 = (void*)(long)status;
    }
    releaseLock();
}

/*
Terminates the current process with the status specified. If the process still has
children, will call join() until it returns -2 (no children remaining) before 
calling quit().

Parameters:
    arg.arg1 - the status to terminate the process with

Returns: N/A (function never returns)
*/
void kernTerminate(USLOSS_Sysargs *arg) {
    acquireLock();
    int status = (int)(long)arg->arg1;
    int joinStatus;

    int ret = join(&joinStatus);
    while (ret != -2) {
        ret = join(&joinStatus);
    }
    releaseLock();
    quit(status);
}

/*
* Creates a semaphore with an intial value read from arg->arg1.
* 
* Parameters:
*     arg: a pointer to a USLOSS_Sysargs struct where the syscall out
*          arguments will be read and stored.
*     arg->arg1: the initial value of the semaphore
* Returns:
*     arg->arg1: the id of the semaphore created
*     arg->arg4: stores 0 if a semaphore was successfully created, -1 otherwise
*/
void kernSemCreate(USLOSS_Sysargs* arg) {
    acquireLock();
    if (numberOfSems >= MAXSEMS) {
        arg->arg4 = (void*)(long)-1;
    }
    else {
        int initialValue = (int)(long)arg->arg1;
        semaphoresList[numberOfSems] = initialValue;
        arg->arg1 = (void*)(long)numberOfSems;
        arg->arg4 = (void*)(long)0;
        numberOfSems++;
    }
    releaseLock();
}

/*
* Decrements the semaphore specified by the id in arg->arg1, and
* if the semaphore value is less than 0, it blocks the current 
* process by trying to receive a message from the process' mailbox.
* It then adds a pointer to the process to the linked list at the
* semaphoreBlockedProc array at the semaphore id so it can be unblocked
* later.
* 
* Parameters:
*     arg: a pointer to a USLOSS_Sysargs struct where the syscall out
*          arguments will be read and stored.
*     arg->arg1: id of the semaphore to decrement
* Returns:
*     arg->arg4: 0 if a valid semaphore id was given, -1 otherwise
*/
void kernSemP(USLOSS_Sysargs* arg) {
    acquireLock();
    int id = (int)(long)arg->arg1;
    if (id < 0 || id >= MAXSEMS) {
        arg->arg4 = (void*)(long)-1;
        releaseLock();
        return;
    }
    arg->arg4 = (void*)(long)0;
    
    int pid = getpid();
    semaphoresList[id]--;
    if (semaphoresList[id] < 0) {
        PCB* procList = semaphoreBlockedProc[id];
        if (procList == NULL) {
            semaphoreBlockedProc[id] = &processTable3[pid % MAXPROC];
        }
        else {
            while (procList->nextBlockedProc != NULL) { // adds to tail of list
                procList = procList->nextBlockedProc;
            }
            procList->nextBlockedProc = &processTable3[pid % MAXPROC];         
        }
        releaseLock();
        MboxRecv(processTable3[pid % MAXPROC].mboxNum, NULL, 0);
    }
    releaseLock();
}

/*
* Increments the value of the semaphore specified by the id in arg->arg1
* and if there are any process blocked by this semaphore, unblock the
* process at the head of the list by sending a message to its mailbox,
* then remove it from the list.
* 
* Parameters:
*     arg: a pointer to a USLOSS_Sysargs struct where the syscall out
*          arguments will be read and stored.
*     arg->arg1: id of the semaphore to decrement
* Returns:
*     arg->arg4: 0 if a valid semaphore id was given, -1 otherwise
*/
void kernSemV(USLOSS_Sysargs* arg) {
    acquireLock();
    int id = (int)(long)arg->arg1;
    if (id < 0 || id >= MAXSEMS) {
        arg->arg4 = (void*)(long)-1;
        releaseLock();
        return;
    }
    arg->arg4 = (void*)(long)0;
    semaphoresList[id]++;
    
    if (semaphoreBlockedProc[id] != NULL) {
        PCB* process = semaphoreBlockedProc[id];
        semaphoreBlockedProc[id] = semaphoreBlockedProc[id]->nextBlockedProc;
        process->nextBlockedProc = NULL;
        releaseLock();
        MboxSend(process->mboxNum, NULL, 0);
    }
    releaseLock();
}

/*
* Calls the kernel mode function currentTime and stores the result in arg1
* of the USLOSS_Sysargs struct.
* 
* Parameters:
*     arg: a pointer to a USLOSS_Sysargs struct where the syscall out
*          arguments will be read and stored.
* Returns:
*     arg->arg1: stores the current clock time
*/
void kernGetTimeOfDay(USLOSS_Sysargs* arg) {
    arg->arg1 = (void*)(long)currentTime();
}

/*
* Calls the kernel mode function readtime and stores the result in arg1
* of the USLOSS_Sysargs struct.
*
* Parameters:
*     arg: a pointer to a USLOSS_Sysargs struct where the syscall out
*          arguments will be read and stored.
* Returns:
*     arg->arg1: stores the readtime of the cpu
*/
void kernCPUTime(USLOSS_Sysargs* arg) {
    arg->arg1 = (void*)(long)readtime();
}

/*
* Calls the kernel mode function getpid and stores the result in arg1
* of the USLOSS_Sysargs struct.
*
* Parameters:
*     arg: a pointer to a USLOSS_Sysargs struct where the syscall out
*          arguments will be read and stored.
* Returns:
*     arg->arg1: stores the pid of the current process
*/
void kernGetPID(USLOSS_Sysargs* arg) {
    arg->arg1 = (void*)(long)getpid();
}
