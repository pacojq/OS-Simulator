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
int OperatingSystem_LongTermScheduler();
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_CheckProcessCreationErrors(int, int);
void OperatingSystem_PCBInitialization(int, int, int, int, int);
int OperatingSystem_ObtainMainMemory(int, int);
void OperatingSystem_ReleaseMainMemory(int);

void OperatingSystem_MoveProcessToState(int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_MoveToTheBLOCKEDState(int);
int OperatingSystem_ExtractFromReadyToRun(int);
int OperatingSystem_ExtractFromSleeping();
int OperatingSystem_WakeUpProcesses(int);
void OperatingSystem_PrintReadyToRunQueue();

void OperatingSystem_PreemptRunningProcess();
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();

void OperatingSystem_HandleClockInterrupt();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_ShowExceptionMessage();


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
int readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};

// Heap with blocked processes sorted by when to wakeup
int sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0;

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

int numberOfClockInterrupts = 0;




// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	OperatingSystem_InitializePartitionTable();
	
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
	ComputerSystem_FillInArrivalTimeQueue();
	//OperatingSystem_PrintStatus();
	OperatingSystem_LongTermScheduler();
	
	if (numberOfNotTerminatedUserProcesses <= 0 &&
			OperatingSystem_IsThereANewProgram() == -1)
		OperatingSystem_ReadyToShutdown();
	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show message "ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime(SHUTDOWN);
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
  
	int PID, index, createdProcesses=0;
	
	while (OperatingSystem_IsThereANewProgram() > 0) {
		index = ComputerSystem_ExtractFromArrivalTimeQueue();
		PID = OperatingSystem_CreateProcess(index);
		
		if (!OperatingSystem_CheckProcessCreationErrors(index, PID)) {
			createdProcesses++;
			if (programList[index]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
	}
	
	if (createdProcesses > 0)
		OperatingSystem_PrintStatus(); 

	// Return the number of succesfully created processes
	return createdProcesses;
}

int OperatingSystem_CheckProcessCreationErrors(int index, int PID) {
	PROGRAMS_DATA *program =programList[index];

	int _error = 1;
	switch (PID) {

		case NOFREEENTRY:
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(103, ERROR, program->executableName);
			break;

		case PROGRAMDOESNOTEXIST:
		OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104, ERROR, program->executableName, "it does not exist");
			break;

		case PROGRAMNOTVALID:
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104, ERROR, program->executableName, "invalid priority size");
			break;

		case TOOBIGPROCESS:
			OperatingSystem_ShowTime(ERROR);
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
	int processSize, partition, priority;
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
 	partition = OperatingSystem_ObtainMainMemory(processSize, PID);
	
	// "Process [x]->[name] requests [y] memory positions\n"
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(142, SYSMEM, PID,
		executableProgram->executableName, processSize);

	if (partition == TOOBIGPROCESS)
		return partition;

	if (partition < 0) { // Error
		OperatingSystem_ShowTime(ERROR);
		ComputerSystem_DebugMessage(144, ERROR, executableProgram->executableName);
		return partition;
	}


	// Load program in the allocated memory
	int loadProgram = OperatingSystem_LoadProgram(programFile, partitionsTable[partition].initAddress, processSize);
	if (loadProgram == TOOBIGPROCESS)
		return loadProgram;


	// PCB initialization
	OperatingSystem_PCBInitialization(PID, partition, processSize, priority, indexOfExecutableProgram);
	


	// If memory allocaton has succeeded, we occupy the partition //
	OperatingSystem_ShowPartitionTable("before allocating memory");
	
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(143, SYSMEM, partition,
		partitionsTable[partition].initAddress, partitionsTable[partition].size,
		PID, executableProgram->executableName);
	
	partitionsTable[partition].occupied = 1;
	partitionsTable[partition].PID = PID;
	
	OperatingSystem_ShowPartitionTable("after allocating memory");



	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(22,INIT,PID,executableProgram->executableName);
	
	return PID;
}


int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

	int i;
	int occupiedPartitions = 0;
	int biggerPartitions = 0;	
	int partition = -1;

	for (i = 0; i < PARTITIONTABLEMAXSIZE; i ++) {
		
		if (partitionsTable[i].size < processSize) {
			biggerPartitions ++;
			continue;
		}
		
		if (partitionsTable[i].occupied) {
			occupiedPartitions ++;
			continue;
		}
			
		if (partition == -1) {
			partition = i;
			continue;
		}
			
		if (partitionsTable[i].size < partitionsTable[partition].size)
			partition = i;
	}
	
	// Process too big
	if (biggerPartitions == PARTITIONTABLEMAXSIZE)
		return TOOBIGPROCESS;
	
	// Memory full
	if (occupiedPartitions == PARTITIONTABLEMAXSIZE - biggerPartitions)
		return MEMORYFULL;
	
	// Everything OK
 	return partition;
}



void OperatingSystem_ReleaseMainMemory(int PID) {

	int partition = processTable[PID].memoryPartition;

	OperatingSystem_ShowPartitionTable("before releasing memory");

	partitionsTable[partition].occupied = 0;

	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(145, SYSMEM, partition,
		partitionsTable[partition].initAddress, partitionsTable[partition].size,
		PID, OperatingSystem_GetProcessName(PID));

	OperatingSystem_ShowPartitionTable("after releasing memory");
}




// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int memoryPartition, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].memoryPartition = memoryPartition;
	processTable[PID].initialPhysicalAddress = partitionsTable[memoryPartition].initAddress;
	processTable[PID].processSize=processSize;
	OperatingSystem_MoveProcessToState(PID, NEW);
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	processTable[PID].whenToWakeUp = 0;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].queueID = DAEMONSQUEUE;
		processTable[PID].copyOfPCRegister= partitionsTable[memoryPartition].initAddress;
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
	if (Heap_add(PID, readyToRunQueue[queue], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[queue], PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_MoveProcessToState(PID, READY);
	}
}

void OperatingSystem_MoveToTheBLOCKEDState(int PID) {
	if (Heap_add(PID, sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses, PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_SaveContext(PID);
		OperatingSystem_MoveProcessToState(PID, BLOCKED);
	}
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
int OperatingSystem_ExtractFromReadyToRun(int queueID) {  
	int selectedProcess=NOPROCESS;
	selectedProcess = Heap_poll(readyToRunQueue[queueID], QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[queueID]);
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}

// Return PID of more priority process in the READY queue
int OperatingSystem_GetFromReadyToRun(int queueID) {
    int selectedProcess=NOPROCESS;
	selectedProcess = Heap_getFirst(readyToRunQueue[queueID], numberOfReadyToRunProcesses[queueID]);
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}

int OperatingSystem_ExtractFromSleeping() {  
	int selectedProcess=NOPROCESS;
	selectedProcess = Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses);
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
	
	Processor_SetAccumulator(processTable[PID].copyOfAccumulator);

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
	processTable[PID].copyOfAccumulator = Processor_GetAccumulator();
}


// Exception management routine
void OperatingSystem_HandleException() {
  
	OperatingSystem_ShowExceptionMessage();
	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
	
}

void OperatingSystem_ShowExceptionMessage() {

	int PID = executingProcessID;
	char* processName = OperatingSystem_GetProcessName(PID);
	char* message;

	switch (Processor_GetRegisterB()) {

		case INVALIDADDRESS:
			message = "invalid address"; break;

		case INVALIDPROCESSORMODE:
			message = "invalid processor mode"; break;

		case DIVISIONBYZERO:
			message = "division by zero"; break;

		case INVALIDINSTRUCTION:
			message = "invalid instruction"; break;

		default: message = "Unknown exception";
	}

	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(140, INTERRUPT, PID, processName, message);
	
}


void OperatingSystem_HandleClockInterrupt() {

	int newPID;
	int oldExecutingPID = executingProcessID;
	int awakenProcesses, createdProcesses;

	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120, INTERRUPT, numberOfClockInterrupts);

	awakenProcesses = OperatingSystem_WakeUpProcesses(numberOfClockInterrupts);
	createdProcesses = OperatingSystem_LongTermScheduler();
	
	if (numberOfNotTerminatedUserProcesses <= 0 &&
			OperatingSystem_IsThereANewProgram() == -1)
		OperatingSystem_ReadyToShutdown();	
	
	// Try to get the highest priority process waking up
	if (awakenProcesses + createdProcesses > 0) {
				
		OperatingSystem_PrintStatus();
		newPID = OperatingSystem_ShortTermScheduler(); // Extract from ready to run

		// Check if the most priority process has to be set to EXECUTING
		if (Heap_compare(newPID, executingProcessID, QUEUE_PRIORITY) > 0 ||
				processTable[executingProcessID].queueID == DAEMONSQUEUE) {

			OperatingSystem_PreemptRunningProcess();
			OperatingSystem_Dispatch(newPID);

			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(121, SHORTTERMSCHEDULE, oldExecutingPID, newPID);

			OperatingSystem_PrintStatus();
		}
		// Otherwise, put it in ready to run again
		else {
			int queue = processTable[newPID].queueID;
			Heap_add(newPID, readyToRunQueue[queue], QUEUE_PRIORITY,
				&numberOfReadyToRunProcesses[queue], PROCESSTABLEMAXSIZE);
		}
	}
}

// Wakes up processes for a given clock tick and returns the number of awaken processes
int OperatingSystem_WakeUpProcesses(int clockTick) {

	int sleepingPID;
	int numberOfAwakenProcesses = 0;

	while (numberOfSleepingProcesses > 0) {
		
		sleepingPID = Heap_getFirst(sleepingProcessesQueue, numberOfSleepingProcesses);
		if (processTable[sleepingPID].whenToWakeUp != clockTick)
			break;

		sleepingPID = OperatingSystem_ExtractFromSleeping();
		OperatingSystem_MoveToTheREADYState(sleepingPID);

		numberOfAwakenProcesses ++;
	}

	return numberOfAwakenProcesses;
}




// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;

	OperatingSystem_ReleaseMainMemory(executingProcessID);
	OperatingSystem_MoveProcessToState(executingProcessID, EXIT);

	processTable[executingProcessID].busy=0;

	// One more process that has terminated
	numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses<=0 &&
			OperatingSystem_IsThereANewProgram() != 1) { // Wait for programs
		// Simulation must finish 
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess = OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}


// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	// Register A contains the identifier of the issued system call
	int systemCallID = Processor_GetRegisterA();

	char* processName = OperatingSystem_GetProcessName(executingProcessID);

	int currentPriority;	
	int newPID;


	switch (systemCallID) {

		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(24,SYSPROC,executingProcessID);
			break;


		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(25,SYSPROC,executingProcessID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
			

		case SYSCALL_YIELD: // Substitute by a USERPROCESS with the same priority
			currentPriority = processTable[executingProcessID].priority;
			newPID = OperatingSystem_GetFromReadyToRun(USERPROCESSQUEUE);
			
			if (newPID != NOPROCESS)
				if (processTable[newPID].priority == currentPriority) {
					newPID = OperatingSystem_ExtractFromReadyToRun(USERPROCESSQUEUE);

					OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
					ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, executingProcessID, newPID);
					OperatingSystem_PreemptRunningProcess();
					OperatingSystem_Dispatch(newPID);
					OperatingSystem_PrintStatus();
				}
			break;
			

		case SYSCALL_SLEEP:
		
			processTable[executingProcessID].whenToWakeUp = numberOfClockInterrupts + 1 +
				abs(Processor_GetAccumulator());

			OperatingSystem_MoveToTheBLOCKEDState(executingProcessID);
			executingProcessID=NOPROCESS;
		
			newPID = OperatingSystem_ShortTermScheduler();
			OperatingSystem_Dispatch(newPID);

			OperatingSystem_PrintStatus();
			break;


		default: // Unknown system call
			OperatingSystem_ShowTime(INTERRUPT);
			ComputerSystem_DebugMessage(141, INTERRUPT, executingProcessID, processName, systemCallID);

			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint) {
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCK_INT_BIT: // CLOCK_INT_BIT=9
			OperatingSystem_HandleClockInterrupt();
			break;
	}
}



void OperatingSystem_MoveProcessToState(int PID, int newState) {

	OperatingSystem_ShowTime(SYSPROC);
	if (newState == NEW)
		ComputerSystem_DebugMessage(111, SYSPROC, PID, statesNames[NEW]);
	else
		ComputerSystem_DebugMessage(110, SYSPROC, PID,
			statesNames[processTable[PID].state], statesNames[newState]);

	processTable[PID].state = newState;
}




void OperatingSystem_PrintReadyToRunQueue() {

	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
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


int OperatingSystem_GetExecutingProcessID() {
	return executingProcessID;
}

char* OperatingSystem_GetProcessName(int PID) {
	return programList[processTable[PID].programListIndex]->executableName;
}