/************************
 * Mike Thweatt
 * CS344
 * Project 3
 * 05/27/18
 * *********************/

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_COMMAND 2048 		//Command line max length
#define MAX_ARGS 512 			//Max num of arguments
char* args[MAX_ARGS]; 			//Array to hold arguments
int exitStatus; 			//Int to hold exit status
char status[256];			//Holds exit status
int argNum = 0; 			//Number of given arguments
int sourceFD = -5; 			//Source file descriptor for redirects
int targetFD = -5; 			//Target file descriptor for redirects
pid_t pidArray[100]; 			//Array to hold background PIDS
int totalPIDS = 0; 			//Number of backgrounded PIDS
pid_t thisPID; 				//This processes PID
int allowBackground = 1; 		//Backgrounding toggle - default true
int isChild = 0; 			//Determine if child or parent
int childRunning = 0; 			//Flag for "is child running"
struct sigaction SIGINT_action = {0}; 	//CTRL+C actions
struct sigaction SIGTSTP_action = {0}; 	//CTRL+Z actions 	
struct sigaction ignore_action = {0}; 	//Ignore signals
struct sigaction default_action = {0};  //Handle as default



/****************************************
 * Parse user input into argument array
 ***************************************/
int parseInput(char* input){
	char* token;
	int count = 0;

	//Get tokens
	token = strtok(input, " \n"); //Get first token
	while (token != NULL) {
		if (count <= MAX_ARGS){
			args[count] = malloc((strlen(token)+1) * sizeof(char));
			memset(args[count], '\0', sizeof(args[count]));
			strcpy(args[count], token);
			count++;
			token = strtok(NULL, " \n");
		} else {
			perror("Number of given arguments has exceeded capacity.\n");
			exit(1);
		}
	}
	return count;
}



/**************************************************************
 * Free dynamic memory and reset variables between user inputs
 *************************************************************/
void cleanInput(){
	//Free memory for next command
	//Free args array
	if (argNum > 0){
		int i;
		for (i = 0; i < argNum; i++){
			free(args[i]);
			args[i] = NULL;
		}
	}
	argNum = 0;
	childRunning = 0;
}



/****************************
 * Change Directory Function
 ***************************/
int cdFunction(){
	if (argNum == 1){
		return chdir(getenv("HOME"));
	} else {
		return chdir(args[1]);
	}

	/*char cwd[1024];
	if(getcwd(cwd, sizeof(cwd)) != NULL);
		printf("%s\n", cwd);*/
}



/***********************************************
 * Print last exit status or terminating signal
 **********************************************/
int printStatus(){printf("%s\n", status); return 0;}



/************************************************
 * Remove single specified element of args array
 ***********************************************/
void argsRemove(int i){
	int j;
	if (j < argNum-1){
		//Shuffle all but last array elements down the array
		for (j = i; j < argNum-1; j++){
			args[j] = args[j+1];
		}
	}
	argNum--;
	free(args[argNum]);
	args[argNum] = NULL; //Set end of array to NULL
}



/**************************************************
 * Remove '&' or '<>' from args array, expand '$$'
 *************************************************/
void scrubArgs(){
	int i;
	//Backgrounding
	if (strcmp(args[argNum-1], "&") == 0)
		argsRemove(argNum-1);

	for (i = argNum-1; i >= 0; i--){
		//Redirections
		if (strcmp(args[i], "<") == 0 || strcmp(args[i], ">") == 0){
			argsRemove(i+1);
			argsRemove(i);
		}
	}
}



/**********************************************************************
 * Replace a given substring with a new given substring
 * Return void: edits original string
 * Modified / help from:
 * www.intechgrity.com/c-program-replacing-a-substring-from-a-string/# 
 *********************************************************************/
void replaceSubstring(char* input, char* replaceThis, char* withThis){
	char newString[MAX_COMMAND];
	char *position;

	//Make sure specified string is in input
	if (!(position = strstr(input, replaceThis)))
		return; //return untouched

	//Copy input to temp 'newString'	
	memset(newString, '\0', sizeof(newString));
	strncpy(newString, input, position-input);

	sprintf(newString + (position - input), "%s%s", withThis, position + strlen(replaceThis));

	//Copy back to input string
	memset(input, '\0', sizeof(input));
	strcpy(input, newString);

	//Recurse to get multiple occurances
	return replaceSubstring(input, replaceThis, withThis);
}



/**************************************************************
 * Simple function to print contents of args array for testing
 *************************************************************/
void printArgs(){
	int i;
	for (i = 0; i < argNum; i++){
		printf("%s\n", args[i]);
	}
}



/*****************************
 * Set any detected redirects
 ****************************/
void setRedirects(int background){
	int i, input=0, output=0;
	//Look for redirections
	for (i = 0; i < argNum; i++){
		if (strstr(args[i], "<") != NULL){
			//Redirect input
			sourceFD = open(args[i+1], O_RDONLY);
			if (sourceFD == 01) {
				perror("source open failure"); 
				exit(1);}
			int result1 = dup2(sourceFD, 0);
			if (result1 == -1) {
				perror("dup2 source failure"); 
				exit(1);}
			input = 1;
		}
		if (strstr(args[i], ">") != NULL) {
			//Redirect output
			targetFD = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (targetFD == -1) {
				perror("target open failure"); 
				exit(1);}
			int result2 = dup2(targetFD, 1);
			if (result2 == -1) {
				perror("dup2 target failure"); 
				exit(1);}
			output = 1;
		}
	}

	//If no redirects were given, set to '/dev/null' for backgrounders only
	if (background == 1){
		if (input == 0){
			//sourceFD = open("/dev/null", O_RDONLY);
			int result1 = dup2(open("/dev/null", O_RDONLY), 0);
			if (result1 == -1) {perror("dup2 source (/dev/null) failure"); exit(1);}
		}
		if (output ==0){
			int result2 = dup2(open("/dev/null", O_WRONLY), 0);
			if (result2 == -1) {perror("dup2 target (/dev/null) failure"); exit(1);}
		}
	}
}



/*************************
 * Add given PID to array
 ************************/
void addPID(pid_t newPID){
	pidArray[totalPIDS] = newPID;
	totalPIDS++;
}



/******************************
 * Remove given PID from array
 *****************************/
void removePID(pid_t newPID){
	int i, j;
	for (i = 0; i < totalPIDS; i++){
		if (pidArray[i] == newPID)
			j = i; break;
	}
	for (i = j; i < totalPIDS; i++){
		pidArray[i] = pidArray[i+1];
	}
	totalPIDS--;
}



/***********************************************************
 * Check for any remaining PIDS in the array for completion
 **********************************************************/
void checkPIDS(){
	int i, j;
	int childExitMethod = 0;
	if (totalPIDS > 0){
		j = totalPIDS - 1;
		for (i = j; i >= 0; i--){
			//Check if PID is complete (0=not complete)
			if (waitpid(pidArray[i], &childExitMethod, WNOHANG)){
				if (WIFEXITED(childExitMethod)){
					//Exited normally
					printf("background pid %d is done: exit value %d\n", 
						pidArray[i], 
						WEXITSTATUS(childExitMethod)); 
					fflush(stdout);
					removePID(pidArray[i]);

				} else if (WIFSIGNALED(childExitMethod)){
					//Exited via signal
					printf("background pid %d is done: terminated by signal %d\n", 
						pidArray[i],
						WTERMSIG(childExitMethod));
					fflush(stdout);
					removePID(pidArray[i]);
				}
			}
		}
	}
}


/******************************************
 * Kill any remaining children before exit
 *****************************************/
void killPIDS(){
	int i;
	if (totalPIDS > 0){
		for (i = 0; i < totalPIDS; i++){
			kill(pidArray[i], SIGTERM);
		}
	}
}



/********************************
 * SIGINT signal handler (CTL+C)
 *******************************/
void catchSIGINT(int signo){
	if (childRunning){
		char* message = "terminated by signal 2\n";
		write(STDOUT_FILENO, message, 23); 
		fflush(stdout);
	}
}



/**********************************
 * SIGTSTP signal handler (CTL+Z) 
 * Toggles "allow background" mode
 **********************************/
void catchSIGTSTP(int signo){
	int childExitMethod;
	wait(&childExitMethod);
	
	if (allowBackground == 1){
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
		allowBackground = 0;
	} else {
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
		allowBackground = 1;
	}
	fflush(stdout);
}



/*************************************************************
 * Set any adjustments to signal handlers for the child forks
 ************************************************************/
void childSignals(int background){
	//Child ignores CTRL+Z
	sigaction(SIGTSTP, &ignore_action, NULL);

	if (background == 0){
		//Foreground child processes default CTRL+C action
		sigaction(SIGINT, &default_action, NULL);
	} else {
		//Background process ignores CTRL+C
		sigaction(SIGINT, &ignore_action, NULL);
	}
}



/***********************************************
 * Set up initial values for signal handlers
 * as well as setup values specific for parent
 **********************************************/
void initializeSigHandlers(){
	//SIGTSTP handler - toggle background mode
	SIGTSTP_action.sa_handler = catchSIGTSTP;

	//Handler to Ignore signals
	ignore_action.sa_handler = SIG_IGN;

	//SIGINT Handler
	SIGINT_action.sa_handler = catchSIGINT;
	SIGINT_action.sa_flags = SA_RESTART; //Allows waitPID to restart

	//Default handler
	default_action.sa_handler = SIG_DFL;

	//Activate parent signal handlers
	sigaction(SIGTSTP, &SIGTSTP_action, NULL); //Parent handles SIGTSTP
	sigaction(SIGINT, &SIGINT_action, NULL); //Parent handles SIGINT
}	



/**************************************************
 * Run any other command given via child processes
 *************************************************/
void runCommand(){
	int childExitMethod = 0;
	int i, background = 0; //Start false unless changed

	//Determine if background process needed
	if (strcmp(args[argNum-1], "&") == 0 && allowBackground)
		background = 1;
	else
		childRunning = 1;


	pid_t childPID = fork();
	switch(childPID){

	case -1: //Error with fork
		perror("fork error\n");
		exit(1); break;

	case 0: //Child
		isChild = 1;
		//Set any redirections if needed
		setRedirects(background);
		//Scrub arguments array for '&' or '<>' before exec
		scrubArgs();
		//Adjust signal handlers
		childSignals(background);
		//Run command via execvp
		execvp(args[0], args);
		perror("exec failure");
		memset(status, '\0', sizeof(status));
		sprintf(status, "%s", "exit value 1");
		exit(1); break;

	default: //Parent
		if (background == 0){ //Foreground process
			int receivedPID = waitpid(childPID, &childExitMethod, 0);

			//Set status
			if (WIFEXITED(childExitMethod)){
				//Exited normally
				memset(status, '\0', sizeof(status));
				sprintf(status, "%s%d", "exit value ",
					WEXITSTATUS(childExitMethod));

			} else if (WIFSIGNALED(childExitMethod)){
				//Exited via signal
				memset(status, '\0', sizeof(status));
				sprintf(status, "%s%d", "terminated by signal ",
					WTERMSIG(childExitMethod));
			}

		} else { //Background process
			waitpid(childPID, &childExitMethod, WNOHANG);
			printf("background pid is %d\n", childPID); 
			fflush(stdout);
			//Add childPID to array to keep track of
			addPID(childPID);
		}
	}
}



/*******
 * MAIN
 ******/
int main(){
	char* userInput = NULL;
	size_t len = 0;
	int i, exitFlag = 0;
	char charPID[10];
	memset(charPID, '\0', sizeof(charPID));

	thisPID = getpid(); //Store PID on start
	sprintf(charPID, "%d", thisPID);


	initializeSigHandlers();

	do{
		printf(": ");
		fflush(stdout);

		if (getline(&userInput, &len, stdin) == -1){
			clearerr(stdin);
		} else {


			//Verify command length
			if (len > MAX_COMMAND){
				perror("Command character length has exceeded capacity.\n");
				exit(1);
			}


			//Expand '$$'
			replaceSubstring(userInput, "$$", charPID);

			//Ignore lines starting with "#" or blank lines (newline char)
			if (userInput[0] != 35 && userInput[0] != 10){
				//Parse input
				argNum = parseInput(userInput);
	
				//Determine command to run
				if (strcmp(args[0], "cd") == 0){
					exitStatus = cdFunction();
					memset(status, '\0', sizeof(status));
					sprintf(status, "%s%d", "exit value ", exitStatus);

				} else if (strcmp(args[0], "status") == 0){
					exitStatus = printStatus();
					memset(status, '\0', sizeof(status));
					sprintf(status, "%s%d", "exit value ", exitStatus);

				} else if (strcmp(args[0], "exit") == 0){
					exitFlag = 1; //Set exit flag

				} else {
					runCommand();
				}
			}

			//Clear variables for next loop
			cleanInput(); 
		
			//Check for any complete background processes
			checkPIDS();
		}


	} while (exitFlag == 0);

	//Kill any remaining children
	killPIDS();
	//Free dynamic memory
	free(userInput);

	return 0;
}
