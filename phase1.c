/* ------------------------------------------------------------------------
	 phase1.c

	 University of Arizona
	 Computer Science 452
	 Fall 2015

	 ------------------------------------------------------------------------ */

//When to enable/disable interrupts?
//What to do with sentinel? All processes done?

#include "phase1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (char *);
extern int start1 (char *);
void dispatcher(void);
void launch();
static void checkDeadlock();
int isInKernelMode();
int isInterruptEnabled();
int enableInterrupts();
void disableInterrupts();
int enterKernelMode();
int enterUserMode();
unsigned int getNextPid();
int isProcessTableFull();
void initProcessTable();
void initReadyLists();
void addProcToReadyLists();
void cleanProcess(procPtr);
void dumpProcesses();
int   zap(int pid);
int   isZapped(void);
int   getpid(void);
int   blockMe(int block_status);
int   unblockProc(int pid);
int   readCurStartTime(void);
void  timeSlice(void);
int   readtime(void);
void  clockHandler(int dev, void * arg);
int countProcesses();
void illegalInstructionHandler(int dev, void *arg);
void timeSlice(void);
int readCurStartTime(void);
int onReadyList(int pid, int priority);


/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 0;

// the process table
procStruct ProcTable[MAXPROC];

// Process lists
// static procPtr ReadyList;  

static procPtr ReadyLists[SENTINELPRIORITY]; //linked list (queue) for each priority

// current process ID
procPtr Current = NULL;

// the next pid to be assigned
unsigned int nextPid = 0;


/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
	 Name - startup
	 Purpose - Initializes process lists and clock interrupt vector.
						 Start up sentinel process and the test process.
	 Parameters - argc and argv passed in by USLOSS
	 Returns - nothing
	 Side Effects - lots, starts the whole thing
	 ----------------------------------------------------------------------- */
void startup(int argc, char *argv[])
{
		int result; /* value returned by call to fork1() */

		// Initialize the clock interrupt handler
		USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;
		USLOSS_IntVec[USLOSS_ILLEGAL_INT] = illegalInstructionHandler;

		/* initialize the process table */
		if (DEBUG && debugflag)
				USLOSS_Console("startup(): initializing process table, ProcTable[]\n");
		initProcessTable();


		// Initialize the Ready list, etc.
		if (DEBUG && debugflag)
				USLOSS_Console("startup(): initializing the Ready list\n");
		initReadyLists();

		

		// startup a sentinel process
		if (DEBUG && debugflag)
				USLOSS_Console("startup(): calling fork1() for sentinel\n");
		result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
										SENTINELPRIORITY);
		if (result < 0) {
				if (DEBUG && debugflag) {
						USLOSS_Console("startup(): fork1 of sentinel returned error, ");
						USLOSS_Console("halting...\n");
				}
				USLOSS_Halt(1);
		}
	
		// start the test process
		if (DEBUG && debugflag)
				USLOSS_Console("startup(): calling fork1() for start1\n");
		result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
		if (result < 0) {
				USLOSS_Console("startup(): fork1 for start1 returned an error, ");
				USLOSS_Console("halting...\n");
				USLOSS_Halt(1);
		}

		USLOSS_Console("startup(): Should not see this message! ");
		USLOSS_Console("Returned from fork1 call that created start1\n");

		return;
} /* startup */


/* ------------------------------------------------------------------------
	 Name - finish
	 Purpose - Required by USLOSS
	 Parameters - none
	 Returns - nothing
	 Side Effects - none
	 ----------------------------------------------------------------------- */
void finish(int argc, char *argv[])
{
		if (DEBUG && debugflag)
				USLOSS_Console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
	 Name - fork1
	 Purpose - Gets a new process from the process table and initializes
						 information of the process.  Updates information in the
						 parent process to reflect this child process creation.
	 Parameters - the process procedure address, the size of the stack and
								the priority to be assigned to the child process.
	 Returns - the process id of the created child or -1 if no child could
						 be created or if priority is not between max and min priority.
	 Side Effects - ReadyList is changed, ProcTable is changed, Current
									process information changed
	 ------------------------------------------------------------------------ */
int fork1(char *name, int (*startFunc)(char *), char *arg,
					int stacksize, int priority)
{
		// test if in kernel mode; halt if in user mode 
		if ( !isInKernelMode() ) {
			USLOSS_Console("fork1(): called while in user mode, by process %d. Halting...\n", getNextPid()-1);
			USLOSS_Halt(1);
		}

		disableInterrupts();
		int procSlot = -1;

		if (DEBUG && debugflag)
				USLOSS_Console("fork1(): creating process %s\n", name);
		

		if (name == NULL || startFunc == NULL) {
			fprintf(stderr, "fork1(): Name and/or start function cannot be null.\n");
			enableInterrupts();
			return -1;
		}

		// test if trying to apply sentinel priority to non-sentinel process
		if (strcmp(name, "sentinel") != 0 && priority == SENTINELPRIORITY) {
			if (DEBUG && debugflag)
				fprintf(stderr, "fork1(): Cannot assign sentinel prority to process other than the sentinel. Halting...\n");
			USLOSS_Halt(1);
		}

		// check for priority out of range
		if (priority > SENTINELPRIORITY || priority < MAXPRIORITY) {
			if (DEBUG && debugflag)
				fprintf(stderr, "fork1(): Priority out of range.\n");
			enableInterrupts();
			return -1;
		}

		// Return if stack size is too small
		if ( stacksize < USLOSS_MIN_STACK ){
			if (DEBUG && debugflag)
				USLOSS_Console("fork1(): Requested Stack size too small.\n");
			enableInterrupts();
			return -2;
		}

		// Is there room in the process table? What is the next PID?
		if (isProcessTableFull()){
			if (DEBUG && debugflag)
				USLOSS_Console("fork1(): Process Table is full.\n");
			enableInterrupts();
			return -1;
		}

		int pid = getNextPid();
		procSlot = (pid - 1) % MAXPROC;

		if (DEBUG && debugflag)
			USLOSS_Console("fork1(): %s's pid is %d\n", name, pid);


		// fill-in entry in process table */
		if ( strlen(name) >= (MAXNAME - 1) ) {
				USLOSS_Console("fork1(): Process name is too long.  Halting...\n");
				USLOSS_Halt(1);
		}

		strcpy(ProcTable[procSlot].name, name);
		ProcTable[procSlot].pid = pid;
		ProcTable[procSlot].priority = priority;
		ProcTable[procSlot].startFunc = startFunc;
		ProcTable[procSlot].stack = (char *) malloc(stacksize * sizeof(char));
		ProcTable[procSlot].stackSize = stacksize;
		ProcTable[procSlot].status = READY;
		ProcTable[procSlot].numJoins = 0;
		ProcTable[procSlot].numKids = 0;
		ProcTable[procSlot].numLiveKids = 0;
		ProcTable[procSlot].startTime = -1;
		ProcTable[procSlot].totalTimeUsed = 0;



		if ( arg == NULL ) {
				ProcTable[procSlot].startArg[0] = '\0';
		}
		else if ( strlen(arg) >= (MAXARG - 1) ) {
				USLOSS_Console("fork1(): argument too long.  Halting...\n");
				USLOSS_Halt(1);
		}
		else {
				strcpy(ProcTable[procSlot].startArg, arg);
		}

		// Initialize context for this process, but use launch function pointer for
		// the initial value of the process's program counter (PC)

		//must enable interrupts before running contextinit
		enableInterrupts();


		USLOSS_ContextInit(&(ProcTable[procSlot].state),
											 ProcTable[procSlot].stack,
											 ProcTable[procSlot].stackSize,
											 NULL,
											 launch);

		disableInterrupts();

		// for future phase(s)
		p1_fork(ProcTable[procSlot].pid);

		//append this new process to current's list of children
		if (Current != NULL) {
			if (Current->childProcPtr == NULL) {
				Current->childProcPtr = &ProcTable[procSlot];
			}
			else {
				procPtr prev = NULL;
				procPtr curr = Current->childProcPtr;
				while (curr != NULL) {
					prev = curr;
					curr = curr->nextSiblingPtr;
				}
				prev->nextSiblingPtr = &ProcTable[procSlot];
			}
			ProcTable[procSlot].parentPtr = Current;
			Current->numKids++;
			Current->numLiveKids++;

		}


		// More stuff to do here...
		addProcToReadyLists(procSlot, priority);

		if (0 != strcmp(ProcTable[procSlot].name, "sentinel")) { // do not call dispatcher when creating sentinel
			if (DEBUG && debugflag)
				USLOSS_Console("fork1(): calling dispatcher()\n");
			enableInterrupts();
			dispatcher();
		}

		enableInterrupts();

		return pid;
} /* fork1 */

/* ------------------------------------------------------------------------
	 Name - launch
	 Purpose - Dummy function to enable interrupts and launch a given process
						 upon startup.
	 Parameters - none
	 Returns - nothing
	 Side Effects - enable interrupts
	 ------------------------------------------------------------------------ */
void launch()
{
		int result;

		if (DEBUG && debugflag)
				USLOSS_Console("launch(): started\n");

		// Enable interrupts
		result = enableInterrupts();


		if (result == -1) {
			fprintf(stderr, "launch(): failed to enable interrupts.\n");
		}

		// Call the function passed to fork1, and capture its return value
		result = Current->startFunc(Current->startArg);

		if (DEBUG && debugflag)
				USLOSS_Console("Process %d returned to launch\n", Current->pid);

		quit(result);

} /* launch */


/* ------------------------------------------------------------------------
	 Name - join
	 Purpose - Wait for a child process (if one has been forked) to quit.  If 
						 one has already quit, don't wait.
	 Parameters - a pointer to an int where the termination code of the 
								quitting process is to be stored.
	 Returns - the process id of the quitting child joined on.
						 -1 if the process was zapped in the join
						 -2 if the process has no children
	 Side Effects - If no child process has quit before join is called, the 
									parent is removed from the ready list and blocked.
	 ------------------------------------------------------------------------ */
int join(int *status)
{
	if ( !isInKernelMode() ) {
			USLOSS_Console("join(): called while in user mode, by process %d. Halting...\n", Current->pid);
			USLOSS_Halt(1);
		}
	disableInterrupts();

	if (Current->childProcPtr == NULL && Current->quitList == NULL) {
		if (DEBUG && debugflag)
			USLOSS_Console("join(): %d has no children\n", Current->pid);
		enableInterrupts();
		return -2; // has no children
	}

	if (Current->numJoins == Current->numKids) {
		if (DEBUG && debugflag)
			USLOSS_Console("join(): already joined for each child\n");
		enableInterrupts();
		return -2; // already joined for each child
	}

	if (Current->quitList != NULL) { // child has already quit
		if (DEBUG && debugflag)
			USLOSS_Console("Join(): Child has already quit\n");
	}
	else { 
		if (DEBUG && debugflag)
			USLOSS_Console("Join(): Must wait for child\n");
		Current->status = JOINBLOCKED;
		enableInterrupts();
		dispatcher();
		disableInterrupts();
	}

	procPtr quitChild = Current->quitList;
	Current->quitList = Current->quitList->quitNext;
	*status = quitChild->quitStatus;
	Current->numJoins++;
	int pid = quitChild->pid;
	cleanProcess(quitChild);

	enableInterrupts();
	// dispatcher(); // FIXME: needed?
	if (isZapped()) {
		return -1;
	}
	return pid;

} /* join */


/* ------------------------------------------------------------------------
	 Name - quit
	 Purpose - Stops the child process and notifies the parent of the death by
						 putting child quit info on the parents child completion code
						 list.
	 Parameters - the code to return to the grieving parent
	 Returns - nothing
	 Side Effects - changes the parent of pid child completion status list.
	 ------------------------------------------------------------------------ */
void quit(int status)
{
	if ( !isInKernelMode() ) {
		USLOSS_Console("quit(): called while in user mode, by process %d. Halting...\n", Current->pid);
		USLOSS_Halt(1);
	}
	disableInterrupts();

	procPtr temp = Current->childProcPtr;
	while (temp != NULL) { // Report error if trying to terminate a process who still has running children
		if (temp->status != QUIT && temp->status != EMPTY) {
			fprintf(stderr, "quit(): process %d, '%s', has active children. Halting...\n", Current->pid, Current->name);
			USLOSS_Halt(1);
		}
		temp = temp->nextSiblingPtr;
	}

	// If parent exsts, add quitting child to their quit list, remove the quitting child from their child list, and unblock them if need be
	if (Current->parentPtr != NULL){
		Current->parentPtr->numLiveKids--;

		// Add to quitlist
		if (Current->parentPtr->quitList == NULL) {
			Current->parentPtr->quitList = Current;
		}
		else {
			procPtr curr = Current->parentPtr->quitList;
			procPtr prev = Current->parentPtr->quitList;
			while (curr != NULL) {
				prev = curr;
				curr = curr->quitNext;
			}
			prev->quitNext = Current;
		}

		// Remove from child list
		procPtr curr = Current->parentPtr->childProcPtr;
		procPtr prev = NULL;
		if (curr->pid == Current->pid) {
			Current->parentPtr->childProcPtr = Current->nextSiblingPtr;
		}
		else {
			while (curr->pid != Current->pid) {
				if (curr == NULL) {
					fprintf(stderr, "quit(): Can't find self in parent's child list to remove self.\n");
					USLOSS_Halt(1);
				}
				prev = curr;
				curr = curr->nextSiblingPtr;
			}
			prev->nextSiblingPtr = curr->nextSiblingPtr;
		}


		// Unblock blocked parent
		if (Current->parentPtr->status == JOINBLOCKED) {
			Current->parentPtr->status = READY;
		}
	}

	// Cleanup process table of all children of the now quit parent (if a parent)
	if (Current->childProcPtr != NULL) {
		procPtr curr = Current->childProcPtr;
		while (curr != NULL) {
			procPtr next = curr->nextSiblingPtr;
			cleanProcess(curr);
			curr = next;
		}
	}

	// Unblock all processes that have zapped me
	if (Current->zapperList != NULL) {
		procPtr curr = Current->zapperList;
		while (curr != NULL) {
			curr->status = READY;
			curr = curr->zapperNext;
		}
	}

	Current->status = QUIT;
	Current->quitStatus = status;

	p1_quit(Current->pid);
	Current = NULL;

	enableInterrupts();

	dispatcher();
} /* quit */


/* ------------------------------------------------------------------------
	 Name - dispatcher
	 Purpose - dispatches ready processes.  The process with the highest
						 priority (the first on the ready list) is scheduled to
						 run.  The old process is swapped out and the new process
						 swapped in.
	 Parameters - none
	 Returns - nothing
	 Side Effects - the context of the machine is changed
	 ----------------------------------------------------------------------- */
void dispatcher(void)
{
	// test if in kernel mode; halt if in user mode 
	if ( !isInKernelMode() ) {
		USLOSS_Console("dispatcher(): called while in user mode, by process %d. Halting...\n", Current->pid);
		USLOSS_Halt(1);
	}
	disableInterrupts();

	//check that sentinel exists
	if (ReadyLists[SENTINELPRIORITY - 1] == NULL){
		USLOSS_Console("dispatcher(): Sentinel does not exist in ready list\n");
		USLOSS_Halt(1);
	}

	procPtr nextProcess = NULL;
	procPtr temp = NULL;
	int i;
	for( i = 0; i < SENTINELPRIORITY; i++){ //loop through each priority
		temp = ReadyLists[i];
		// USLOSS_Console("-----------PRIORITY %d---------------\n", i+1);
		while (temp != NULL && temp->status != READY && temp->status != RUNNING){
			//int nextProcPid = temp->nextProcPtr == NULL? -1 : temp->nextProcPtr->pid;
			// USLOSS_Console("name = %s, pid = %d, status = %d, nextProcPid = %d\n", temp->name, temp->pid, temp->status, nextProcPid);
			temp = temp->nextProcPtr;
		}
		

		// if (temp != NULL){
		// 	int nextProcPid = temp->nextProcPtr == NULL? -1 : temp->nextProcPtr->pid;
		// 	USLOSS_Console("name = %s, pid = %d, status = %d, nextProcPid = %d\n", temp->name, temp->pid, temp->status, nextProcPid);
		// } else {
		// 	USLOSS_Console("null\n");
		// }

		if (temp != NULL){
			nextProcess = temp;
			// USLOSS_Console("------------------------------------\n");		
			break;
		}

		// USLOSS_Console("------------------------------------\n");		

	}

	if (DEBUG && debugflag)
		USLOSS_Console("dispatcher(): found process %s (pid %d) at priority %d\n", temp->name, temp->pid, i+1);



	if (Current == NULL){ //possibly
		p1_switch(-1, nextProcess->pid);
	} else {
		p1_switch(Current->pid, nextProcess->pid);
	}

	//get old and new contexts
	USLOSS_Context * oldContext = Current == NULL ? NULL : &Current->state;
	USLOSS_Context * newContext = &nextProcess->state;

	if (Current != NULL){
		Current->totalTimeUsed = Current->totalTimeUsed + (readtime() - Current->startTime);
		Current->startTime = -1; //FIXME: maybe
	}

	//reset current
	Current = nextProcess;
	Current->status = RUNNING;
	Current->startTime = readtime();

	enableInterrupts();

	//call to context switch
	USLOSS_ContextSwitch(oldContext, newContext);

} /* dispatcher */


/* ------------------------------------------------------------------------
	 Name - sentinel
	 Purpose - The purpose of the sentinel routine is two-fold.  One
						 responsibility is to keep the system going when all other
						 processes are blocked.  The other is to detect and report
						 simple deadlock states.
	 Parameters - none
	 Returns - nothing
	 Side Effects -  if system is in deadlock, print appropriate error
									 and halt.
	 ----------------------------------------------------------------------- */

int sentinel (char *dummy)
{

	if (DEBUG && debugflag)
			USLOSS_Console("sentinel(): called\n");

	if ( !isInKernelMode() ) {
		USLOSS_Console("sentinel(): called while in user mode, by process %d. Halting...\n", Current->pid);
		USLOSS_Halt(1);
	}

	while (1)
	{
		checkDeadlock();
		USLOSS_WaitInt();
	}
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
	int i;
	int blocked = 1;
	for( i = 0; i < MINPRIORITY; i++){ //loop through each priority
		procPtr temp = ReadyLists[i];
		if (temp != NULL) {
			procPtr proc = ReadyLists[i];
			while (proc != NULL) {
				if (proc->status == READY || proc->status == RUNNING) {
					fprintf(stderr, "checkDeadlock(): found another process (name: %s, pid: %d, status: %d) on the ready list.\n", proc->name, proc->pid, proc->status);
					USLOSS_Halt(1);
				}
				blocked = blocked && (proc->status == JOINBLOCKED || proc->status == ZAPBLOCKED || proc->status > MEBLOCKED); //FIXME: maybe not 100% sure about this
				proc = proc->nextProcPtr;
			}
		}
	}

	if (blocked) {
		USLOSS_Console("checkDeadlock(): numProc = %d. Only Sentinel should be left. Halting...\n", countProcesses());
	}
	else {
		USLOSS_Console("All processes completed.\n");
	}
	USLOSS_Halt(0);

} /* checkDeadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
		// turn the interrupts OFF iff we are in kernel mode
		// if not in kernel mode, print an error message and
		// halt USLOSS
	if (isInKernelMode()) {
		unsigned int psr = USLOSS_PsrGet();
		unsigned int op = 0xfffffffd;
		int result = USLOSS_PsrSet(psr & op);
		if (result == USLOSS_ERR_INVALID_PSR) {
			fprintf(stderr, "Failed to set PSR to kernel mode.");
			USLOSS_Halt(0);
		}
	}
	else {
		fprintf(stderr, "Failed to disable interrupts as not in kernel mode.");
		USLOSS_Halt(0);
	}

	// TODO: May need more than just switching the bit?

} /* disableInterrupts */

/*
 * Returns 1 if in kernel mode, else 0.
 */
int isInKernelMode() {
	unsigned int psr = USLOSS_PsrGet();
	unsigned int op = 0x1;
	return psr & op;
}

int enterKernelMode() {
	unsigned int psr = USLOSS_PsrGet();
	unsigned int op = 0x1;
	int result = USLOSS_PsrSet(psr | op);
	if (result == USLOSS_ERR_INVALID_PSR) {
		return -1;
	}
	else {
		return 0;
	}

	// TODO: May need more than just switching the bit?
}

int enterUserMode() {
	unsigned int psr = USLOSS_PsrGet();
	unsigned int op = 0xfffffffe;
	int result = USLOSS_PsrSet(psr & op);
	if (result == USLOSS_ERR_INVALID_PSR) {
		return -1;
	}
	else {
		return 0;
	}
	// TODO: May need more than just switching the bit?
}

/*
 * Returns 1 if interrupts are enabled, else 0.
 */
int isInterruptEnabled() {
	unsigned int psr = USLOSS_PsrGet();
	unsigned int op = 0x2;
	return (psr & op) >> 1;
}

int enableInterrupts() {
	unsigned int psr = USLOSS_PsrGet();
	unsigned int op = 0x2;
	int result = USLOSS_PsrSet(psr | op);
	if (result == USLOSS_ERR_INVALID_PSR) {
		return -1;
	}
	else {
		return 0;
	}

	// TODO: May need more than just switching the bit?
}

/*
	Checks if process table is full
*/

int isProcessTableFull(){
	for (int i = 0; i < MAXPROC; i++){
		if (ProcTable[i].status == EMPTY){
			return 0;
		}
	}
	return 1;
}

/*
	Scans for available pid
*/
unsigned int getNextPid(){
	// USLOSS_Console("---next pid = %d before loop--\n", nextPid);
	do {
		nextPid++;
	} while (ProcTable[(nextPid - 1) % MAXPROC].status != EMPTY);
	// USLOSS_Console("---next pid = %d after loop--\n", nextPid);

	return nextPid;
}

void initProcessTable(){
	for (int i = 0; i < MAXPROC; i++){
		ProcTable[i].status = EMPTY;
	}
}

void initReadyLists(){
	// TODO: do something maybe
}

void addProcToReadyLists(int procSlot, int priority){

	int rl_index = priority - 1;

	if (ReadyLists[rl_index] == NULL){
		ReadyLists[rl_index] = &ProcTable[procSlot];
	} else {
		procPtr prev = NULL;
		procPtr curr = ReadyLists[rl_index];
		while (curr != NULL){
			prev = curr;
			curr = curr->nextProcPtr;
		}
		prev->nextProcPtr = &ProcTable[procSlot];
	}
	if (DEBUG && debugflag)
		USLOSS_Console("fork1(): adding %s to readylist at priority %d\n", ReadyLists[rl_index]->name, priority);
}

void cleanProcess(procPtr proc) {	
	if (DEBUG && debugflag)
		USLOSS_Console("cleanProcess(): removing %d from ReadyList\n", proc->pid);
	//remove proc from ready list
	if (ReadyLists[proc->priority -1] != NULL){

		if (ReadyLists[proc->priority -1]->pid == proc->pid){
			ReadyLists[proc->priority -1] = proc->nextProcPtr;
		} else {
			procPtr fore = ReadyLists[proc->priority -1]->nextProcPtr;
			procPtr aft = ReadyLists[proc->priority -1];
			while (fore != NULL && fore->pid != proc->pid){

				aft = fore;
				fore = fore->nextProcPtr;
			}

			aft->nextProcPtr = proc->nextProcPtr;
		}
	}
	proc->nextProcPtr = NULL;
	proc->childProcPtr = NULL;
	proc->nextSiblingPtr = NULL;
	proc->quitList = NULL;
	proc->quitNext = NULL;
	proc->parentPtr = NULL;
	proc->quitStatus = 0;
	proc->status = EMPTY;
	proc->zapped = 0;
	proc->zapperList = NULL;
	proc->zapperNext = NULL;
	proc->startTime = -1;
	proc->totalTimeUsed = 0;
}

// its PID, parent’s PID, priority, process status (e.g. empty, running, ready, blocked, etc.), number of children, CPU time consumed, and na
void dumpProcesses() {
	char * statuses[6];
	statuses[EMPTY] = "EMPTY";
	statuses[READY] = "READY";
	statuses[RUNNING] = "RUNNING";
	statuses[JOINBLOCKED] = "JOINBLOCKED";
	statuses[ZAPBLOCKED] = "ZAPBLOCKED";
	statuses[QUIT] = "QUIT";

	USLOSS_Console(" SLOT   PID       NAME       PARENTPID   PRIORITY     STATUS     NUM CHILDREN  NUM LIVE KIDS  NUM JOINS   TIME USED \n");
	USLOSS_Console("------ ----- -------------- ----------- ---------- ------------ -------------- ------------- ----------- -----------\n");
	for (int i = 0; i < MAXPROC; i++){
			procPtr temp = &ProcTable[i];
			int parentpid = temp->parentPtr == NULL? -1 : temp->parentPtr->pid;
			if (temp->status > MEBLOCKED)
				USLOSS_Console("%6d %5d %14s %11d %10d %12d %14d %13d %11d %11d\n", i, temp->pid, temp->name, parentpid, temp->priority, temp->status, temp->numKids, temp->numLiveKids, temp->numJoins, temp->totalTimeUsed);
			else 
				USLOSS_Console("%6d %5d %14s %11d %10d %12s %14d %13d %11d %11d\n", i, temp->pid, temp->name, parentpid, temp->priority, statuses[temp->status], temp->numKids, temp->numLiveKids, temp->numJoins, temp->totalTimeUsed);
	}
}

int zap(int pid) {
	if (pid == Current->pid) {
		fprintf(stderr, "zap(): process %d tried to zap itself.  Halting...\n", Current->pid);
		USLOSS_Halt(1);
	}

	int procSlot = (pid - 1) % MAXPROC;

	if (ProcTable[procSlot].status == EMPTY || ProcTable[procSlot].pid != pid) {
		fprintf(stderr, "zap(): process being zapped does not exist.  Halting...\n");
		USLOSS_Halt(1);
	}

	if(ProcTable[procSlot].status == QUIT) {
		if (isZapped()) {
			return -1;
		}
		else {
			return 0;
		}
	}

	ProcTable[procSlot].zapped = 1;
	if (ProcTable[procSlot].zapperList == NULL) {
		ProcTable[procSlot].zapperList = Current;
	}
	else {
		procPtr prev = NULL;
		procPtr curr = ProcTable[procSlot].zapperList;
		while (curr != NULL) {
			prev = curr;
			curr = curr->zapperNext;
		}
		prev->zapperNext = Current;
	}

	Current->status = ZAPBLOCKED;

	dispatcher();

	if (isZapped()) {
		return -1;
	}

	return 0;
}

int isZapped(void) {
	return Current->zapped;
}

int getpid(void) {
	return Current->pid;
}

int blockMe(int block_status) {
	if ( !isInKernelMode() ) {
		USLOSS_Console("blockMe(): called while in user mode, by process %d. Halting...\n", Current->pid);
		USLOSS_Halt(1);
	}

	disableInterrupts();

	if (block_status <= MEBLOCKED){
		USLOSS_Console("blockMe(): cannot block process with status (%d) <= 10. Halting...\n", block_status);
		USLOSS_Halt(1);
	}

	Current->status = block_status;
	dispatcher();
	if (isZapped()){
		enableInterrupts();
		return -1;
	}
	enableInterrupts();
	return 0;
}

int unblockProc(int pid) {
	if ( !isInKernelMode() ) {
		USLOSS_Console("unblockProc(): called while in user mode, by process %d. Halting...\n", Current->pid);
		USLOSS_Halt(1);
	}

	disableInterrupts();

	//returns -2 if proc is the current process, does not exist, not me-blocked, or blocked on status <= 10
	if (pid == Current->pid){
		if (DEBUG && debugflag)
			USLOSS_Console("unblockProc(): attempting to unblock Current process (pid %d).\n", pid);
		enableInterrupts();
		return -2;
	}

	procPtr proc = &ProcTable[(pid - 1) % MAXPROC];
	if (proc->status == EMPTY){
		if (DEBUG && debugflag)
			USLOSS_Console("unblockProc(): attempting to unblock non existant process (pid %d does not exist).\n", pid);
		enableInterrupts();
		return -2;
	}

	if (proc->status <= MEBLOCKED){
		if (DEBUG && debugflag)
			USLOSS_Console("unblockProc(): attempting to unblock process %d with status (%d) <= 10 (not meblocked)\n", pid, proc->status);
		enableInterrupts();
		return -2;
	}

	if (DEBUG && debugflag)
		USLOSS_Console("unblockProc(): unblocking process %d.\n", pid);


	proc->status = READY; //change status to ready

	if (!onReadyList(pid, proc->priority)){ //add to readylist if not already there
		if (DEBUG && debugflag)
			USLOSS_Console("unblockProc(): process %d not found on ready list[%d], adding now.\n", pid, proc->priority);
		addProcToReadyLists((pid - 1) % MAXPROC, proc->priority); //procSlot, priority
	}

	dispatcher();

	if (isZapped()){
		enableInterrupts();
		return -1;
	}
	
	enableInterrupts();
	return 0;
}


int readtime(void) {
	int status = 0;
	int dev_status = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &status);//

	if (dev_status == USLOSS_DEV_OK) {
		return status;
	} else {
		USLOSS_Console("Failed to read current time. Halting...\n");
		USLOSS_Halt(1);
		return -1; //so it compiles without warning
	}
}

void clockHandler(int dev, void *arg) {
	if (DEBUG && debugflag)
		USLOSS_Console("clockHandler(): clock interrupt occurred");
	timeSlice();
}

void illegalInstructionHandler(int dev, void *arg) {
	return;
}

void timeSlice(void){
	int curTime = readtime();
	int startTime = readCurStartTime();
	if (curTime - startTime < 80000){
		if (DEBUG && debugflag)
			USLOSS_Console("timeSlice(): Process %d exceeded time slice with cpu time %d\n, calling dispatcher", Current->pid, curTime-startTime);
		dispatcher();
	} else {
		if (DEBUG && debugflag)
			USLOSS_Console("timeSlice(): Process %d did not exceed with cpu time %d\n", Current->pid, curTime-startTime);
	}
}

int readCurStartTime(void){
	return Current == NULL ? -1 : Current->startTime;
}

int countProcesses() {
	int count = 0;
	for (int i = 0; i < MAXPROC; i++) {
		if (ProcTable[i].status != EMPTY && ProcTable[i].status != QUIT) {
			count++;
		}
	}
	return count;
}

int onReadyList(int pid, int priority){
	procPtr list = ReadyLists[priority-1];
	if (list == NULL){
		return 0;
	}
	procPtr temp = list;
	while (temp != NULL){
		if (temp->pid == pid){
			return 1;
		}
		temp = temp->nextProcPtr;
	}
	return 0;
}
