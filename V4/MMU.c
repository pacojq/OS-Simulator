#include "MMU.h"
#include "Buses.h"
#include "Processor.h"
#include "OperatingSystem.h"
#include <string.h>

//int MMU_CheckRegisterMAR();

// The base register
int registerBase_MMU;

// The limit register
int registerLimit_MMU;

// The MAR register
int registerMAR_MMU;


// Logical address is in registerMAR_MMU. If correct, physical address is produced
// by adding logical address and base register
int MMU_readMemory() {
	 
	if (Processor_PSW_BitState(EXECUTION_MODE_BIT)){ // Protected mode
		if (registerMAR_MMU < MAINMEMORYSIZE && registerMAR_MMU >= 0) {
			// Send to the main memory HW the physical address to write in
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to read
			MainMemory_readMemory();
			return MMU_SUCCESS;
		}
		else {
			Processor_RaiseException(INVALIDADDRESS);
			return MMU_FAIL;
		}
	}
	else {// Non-Protected mode
		if (registerMAR_MMU<registerLimit_MMU && registerMAR_MMU >= 0) {
			// Physical address = logical address + base register
			registerMAR_MMU+=registerBase_MMU;
			// Send to the main memory HW the physical address to write in
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to read
			MainMemory_readMemory();
			return MMU_SUCCESS;
		}
		else {
			Processor_RaiseException(INVALIDADDRESS);
			return MMU_FAIL;
		}
	}
}



// Logical address is in registerMAR_MMU. If correct, physical address is produced
// by adding logical address and base register
int MMU_writeMemory() {

	if (Processor_PSW_BitState(EXECUTION_MODE_BIT)) // Protected mode
		if (registerMAR_MMU < MAINMEMORYSIZE && registerMAR_MMU >= 0) {
			// Send to the main memory HW the physical address to write in
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to read
			MainMemory_writeMemory();
			return MMU_SUCCESS;		
		}
		else {
			Processor_RaiseException(INVALIDADDRESS);
			return MMU_FAIL;
		}
	else   // Non-Protected mode
		if (registerMAR_MMU<registerLimit_MMU && registerMAR_MMU >= 0) {
			// Physical address = logical address + base register
			registerMAR_MMU+=registerBase_MMU;
			// Send to the main memory HW the physical address to read from
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to write
			MainMemory_writeMemory();
			return MMU_SUCCESS;
		}
		else {
			Processor_RaiseException(INVALIDADDRESS);
			return MMU_FAIL;
		}
}



// Setter for registerMAR_MMU
void MMU_SetMAR (int newMAR) {
  registerMAR_MMU = newMAR;
}

// Getter for registerMAR_MMU
int MMU_GetMAR () {
  return registerMAR_MMU;
}

// Setter for registerBase_MMU
void MMU_SetBase (int newBase) {
  registerBase_MMU = newBase;
}

// Getter for registerBase_MMU
int MMU_GetBase () {
  return registerBase_MMU;
}

// Setter for registerLimit_MMU
void MMU_SetLimit (int newLimit) {
  registerLimit_MMU = newLimit;
}

// Getter for registerLimit_MMU
int MMU_GetLimit () {
  return registerLimit_MMU;
}



/*
int MMU_CheckRegisterMAR() {
	if (Processor_PSW_BitState(EXECUTION_MODE_BIT)){ // Protected mode
		if (registerMAR_MMU < MAINMEMORYSIZE && registerMAR_MMU >= 0)
			return -1;
	}
	else {// Non-Protected mode
		if (registerMAR_MMU < registerLimit_MMU && registerMAR_MMU >= 0)
			return -1;
	}
	return 1;
}

// Logical address is in registerMAR_MMU. If correct, physical address is produced
// by adding logical address and base register
int MMU_readMemory() {
	 
	if (MMU_CheckRegisterMAR() < 0) {
		Processor_RaiseException(INVALIDADDRESS);
		return MMU_FAIL;
	}

	if (Processor_PSW_BitState(EXECUTION_MODE_BIT)){ // Protected mode
		
			// Send to the main memory HW the physical address to write in
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to read
			MainMemory_readMemory();
			return MMU_SUCCESS;
	}
	else {// Non-Protected mode
		
			// Physical address = logical address + base register
			registerMAR_MMU+=registerBase_MMU;
			// Send to the main memory HW the physical address to write in
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to read
			MainMemory_readMemory();
			return MMU_SUCCESS;
	}
}



// Logical address is in registerMAR_MMU. If correct, physical address is produced
// by adding logical address and base register
int MMU_writeMemory() {

	if (!MMU_CheckRegisterMAR()) {
		Processor_RaiseException(INVALIDADDRESS);
		return MMU_FAIL;
	}

	if (Processor_PSW_BitState(EXECUTION_MODE_BIT)) {// Protected mode
		
			// Send to the main memory HW the physical address to write in
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to read
			MainMemory_writeMemory();
			return MMU_SUCCESS;
	}
	else {   // Non-Protected mode
		
			// Physical address = logical address + base register
			registerMAR_MMU+=registerBase_MMU;
			// Send to the main memory HW the physical address to read from
			Buses_write_AddressBus_From_To(MMU, MAINMEMORY);
			// Tell the main memory HW to write
			MainMemory_writeMemory();
			return MMU_SUCCESS;
	}
}
*/