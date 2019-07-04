#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>


// Functions prototypes
void OperatingSystem_PrepareDaemons();
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun(int);
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
int OperatingSystem_CheckProcessCreationErrors(int, int);
void OperatingSystem_MoveProcessToState(int, int);

char * statesNames[5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"};


// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
//int readyToRunQueue[PROCESSTABLEMAXSIZE];
//int numberOfReadyToRunProcesses=0;
int readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};


// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// Obtain the memory requirements of the program
	int processSize = OperatingSystem_ObtainProgramSize(&programFile, "OperatingSystemCode");

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++)
		processTable[i].busy=0;
	
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+1);
		
	// Create all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);
	
	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	if (numberOfNotTerminatedUserProcesses == 0)
		exit(0);
	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show message "ERROR: Missing SIP program!\n"
		ComputerSystem_DebugMessage(21,SHUTDOWN);
		exit(1);
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
void OperatingSystem_PrepareDaemons(int programListDaemonsBase) {
  
	// Include a entry for SystemIdleProcess at 0 position
	programList[0]=(PROGRAMS_DATA *) malloc(sizeof(PROGRAMS_DATA));

	programList[0]->executableName="SystemIdleProcess";
	programList[0]->arrivalTime=0;
	programList[0]->type=DAEMONPROGRAM; // daemon program

	sipID=INITIALPID%PROCESSTABLEMAXSIZE; // first PID for sipID

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	baseDaemonsInProgramList=programListDaemonsBase;

}


// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command lineand daemons programs
int OperatingSystem_LongTermScheduler() {
  
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;
	
	for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER ; i++) {
		PID=OperatingSystem_CreateProcess(i);
		if (!OperatingSystem_CheckProcessCreationErrors(i, PID)) {
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
	}
	
	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

int OperatingSystem_CheckProcessCreationErrors(int index, int PID) {
	PROGRAMS_DATA *program =programList[index];

	int _error = 1;
	switch (PID) {

		case NOFREEENTRY:
			ComputerSystem_DebugMessage(103, ERROR, program->executableName);
			break;

		case PROGRAMDOESNOTEXIST:
			ComputerSystem_DebugMessage(104, ERROR, program->executableName, "it does not exist");
			break;

		case PROGRAMNOTVALID:
			ComputerSystem_DebugMessage(104, ERROR, program->executableName, "invalid priority size");
			break;

		case TOOBIGPROCESS:
			ComputerSystem_DebugMessage(105, ERROR, program->executableName);
			break;


		default: // We've found no error
			_error = 0;
	}
	return _error;
}


// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	if (PID == NOFREEENTRY)
		return PID;

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(&programFile, executableProgram->executableName);	
	if (processSize == PROGRAMDOESNOTEXIST || processSize == PROGRAMNOTVALID)
		return processSize;

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	
	// Obtain enough memory space
 	loadingPhysicalAddress=OperatingSystem_ObtainMainMemory(processSize, PID);
	if (loadingPhysicalAddress == TOOBIGPROCESS)
		return loadingPhysicalAddress;


	// Load program in the allocated memory
	int loadProgram = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if (loadProgram == TOOBIGPROCESS)
		return loadProgram;

	
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	// Show message "Process [PID] created from program [executableName]\n"
	ComputerSystem_DebugMessage(22,INIT,PID,executableProgram->executableName);
	
	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

 	if (processSize>MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	
 	return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	OperatingSystem_MoveProcessToState(PID, NEW);
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].queueID = DAEMONSQUEUE;
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
		processTable[PID].copyOfAccumulator=0;
	} 
	else {
		processTable[PID].queueID = USERPROCESSQUEUE;
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
		processTable[PID].copyOfAccumulator=0;
	}

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {	
	int queue = processTable[PID].queueID;
	if (Heap_add(PID, readyToRunQueue[queue], QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[queue] ,PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_MoveProcessToState(PID, READY);
	}
	OperatingSystem_PrintReadyToRunQueue();
}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;
	int queue = 0;
	do {
		selectedProcess=OperatingSystem_ExtractFromReadyToRun(queue);
		queue ++;
	}
	while(selectedProcess == NOPROCESS && queue < NUMBEROFQUEUES);
	
	return selectedProcess;
}


// Return PID and poll the most priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int queueId) {  
	int selectedProcess=NOPROCESS;
	selectedProcess = Heap_poll(readyToRunQueue[queueId], QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[queueId]);
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}

// Return PID of more priority process in the READY queue
int OperatingSystem_GetFromReadyToRun(int queueId) {
    int selectedProcess=NOPROCESS;
	selectedProcess = Heap_getFirst(readyToRunQueue[queueId], numberOfReadyToRunProcesses[queueId]);
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	OperatingSystem_MoveProcessToState(PID, EXECUTING);
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  	
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1, processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2, processTable[PID].copyOfPSWRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-3, processTable[PID].copyOfAccumulator);

	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {
	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
	processTable[PID].copyOfAccumulator = Processor_CopyFromSystemStack(MAINMEMORYSIZE-3);
}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	ComputerSystem_DebugMessage(23,SYSPROC,executingProcessID);
	
	OperatingSystem_TerminateProcess();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
  	
	OperatingSystem_MoveProcessToState(executingProcessID, EXIT);
	
	// One more process that has terminated
	numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses<=0) {
		// Simulation must finish 
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();
	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}


// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID;

	int currentPriority, otherPID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			ComputerSystem_DebugMessage(24,SYSPROC,executingProcessID);
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			ComputerSystem_DebugMessage(25,SYSPROC,executingProcessID);
			OperatingSystem_TerminateProcess();
			break;
			
		case SYSCALL_YIELD:
			currentPriority = processTable[executingProcessID].priority;
			otherPID = OperatingSystem_GetFromReadyToRun(USERPROCESSQUEUE);
			
			if (otherPID != NOPROCESS)
				if (processTable[otherPID].priority == currentPriority) {
					otherPID = OperatingSystem_ExtractFromReadyToRun(USERPROCESSQUEUE);

					ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, executingProcessID, otherPID);
					OperatingSystem_PreemptRunningProcess();
					OperatingSystem_Dispatch(otherPID);
				}
			break;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
	}

}

void OperatingSystem_PrintReadyToRunQueue() {

	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);
	int q, i, PID;
	
	for (q = 0; q < NUMBEROFQUEUES; q ++) {
		
		// Print queue title //
		int msg = -1;
		switch (q) {
			case USERPROCESSQUEUE: 	msg = 112; break;
			case DAEMONSQUEUE: 		msg = 113; break;
		}
		if (msg != -1)
			ComputerSystem_DebugMessage(msg, SHORTTERMSCHEDULE);
		
		
		// Print processes //		
		for(i = 0; i < numberOfReadyToRunProcesses[q]; i ++) {
			PID = readyToRunQueue[q][i];
			ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, PID, processTable[PID].priority);
			
			if (i < numberOfReadyToRunProcesses[q]-1) // Print coma
				ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE);
		}
		ComputerSystem_DebugMessage(109, SHORTTERMSCHEDULE); // New line
	}
	
}


void OperatingSystem_MoveProcessToState(int PID, int newState) {

	if (newState == NEW)
		ComputerSystem_DebugMessage(111, SYSPROC, PID, statesNames[NEW]);
	else
		ComputerSystem_DebugMessage(110, SYSPROC, PID,
			statesNames[processTable[PID].state], statesNames[newState]);

	processTable[PID].state = newState;
}