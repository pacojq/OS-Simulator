#include "Clock.h"
#include "Processor.h"
#include "ComputerSystem.h"

#define INTERVALBETWEENINTERRUPS 5

int tics=0;

void Clock_Update() {
	tics++;
	if (tics % INTERVALBETWEENINTERRUPS == 0)
		Processor_RaiseInterrupt(CLOCK_INT_BIT);    
}


int Clock_GetTime() {

	return tics;
}
