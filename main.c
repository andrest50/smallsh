#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <signal.h>

//These are all of the function prototypes in the program
void changeDir(char*);
char* checkVariableExpansion(char*, char*);
void freeMemory(char*, char*, char*, char**, int);
void setCommand(char*, char**, int);
void handle_SIGINT_fg(int signo);
void handle_SIGTSTP(int signo);
void redirectSTDOUT(char**, int);
void redirectSTDIN(char**, int, int*);
void runCommand(char**, int*, int, int, int**, int*, struct sigaction*);
void checkBgProcesses(int**, int);
void parseUserInput(char*, char**, int*, int*);
void killChildProcesses(int**, int);

/*Global variables to keep track of the type of the child process status
and current foreground-only mode*/
int status_flag = 0;
int foreground_mode = 0;

/*
* This a built-in function for changing the current directory.
* It acts as a custom version of the cd linux command.
*/
void changeDir(char* dir) {
	char curr_dir[200]; //string used for printing the current directory
	int val = chdir(dir); //change the directory and return whether it was valid or not
	if (val == -1) {
		perror("");
		fflush(stdout);
	}
}

/*
* Finds any instance of $$ in the string from the command line
* and replaces them with the process ID for the running program
*/
char* checkVariableExpansion(char* input, char* newString) {
	int currChar = 0; //keeps track of the current character being checked
	int numChar = 0; //counts the number of characters since an occurrence of $$
	int startingChar = 0; //keeps track of the starting character from last occurrence of $$
	char saveInput[1024]; //used to duplicate the input in case the input is altered

	strcpy(saveInput, input);

	//Loops through the entire input, replacing instances of $$ with the process ID
	for (int i = currChar; i < strlen(saveInput)-1; i++) {
		numChar++;
		currChar++;

		/*In the case that the current and next character are $
		copy from the last occurrence of $$ until the current
		occurrence of $$, and append the process ID to the string
		using sprintf(). Then set the new startingChar and numChar variables.*/
		if (input[i] == '$' && input[i + 1] == '$') {
			strncat(newString, input+startingChar, numChar-1);
			char pid[10];
			sprintf(pid, "%d", getpid());
			strcat(newString, pid);
			printf("%s\n", newString);
			i++;
			startingChar += (numChar+1);
			numChar = 0;
			currChar++;
		}
	}

	//Get the last bit of the input string before returning the updated string
	strncat(newString, input + startingChar, numChar);

	return newString;
}

/*
* Frees all of the memory used within the main function
* (frees variables involved in command line operations)
*/
void freeMemory(char* preInput, char* postInput, char* checkString, char** args, int num) {

	/*If there were no arguments, the first string within the multi-dimensional
	strings needs to be freed since it was still allocated*/
	if (num == 0) {
		free(args[0]);
	}
	else {
		//Frees the command-line arguments
		for (int i = 0; i < num; i++) {
			free(args[i]);
		}
	}

	//Frees the rest of the variables used
	free(checkString);
	free(preInput);
	free(postInput);
}

/*
* Sets the command portion of the user input from the command line.
* The first string/token from the user input is the command.
*/
void setCommand(char* command, char** args, int numArgs) {

	//If the command is not empty then copy the first argument to the command variable
	if (numArgs > 0) {
		strcpy(command, args[0]);
		strtok(command, "\n"); //Remove any newline that may be present
		fflush(stdout);
	}
	//Set the command to be an empty string if there are no arguments from the user
	else {
		strcpy(command, "");
	}
}
/*Signal handler for SIGINT when the child process is a foreground process*/
void handle_SIGINT_fg(int signo) {
	status_flag = 2;
	char message[25] = "terminated by signal 2\n";
	char signoString[25];
	sprintf(signoString, "terminated by signal %d\n", signo);
	write(STDOUT_FILENO, message, 25);
	signal(SIGINT, SIG_DFL); //set the SIGINT to run in default
}

/*Signal handler for SIGTSTP to set foreground-only mode*/
void handle_SIGTSTP(int signo) {

	//Toggle the foreground-only mode depending on its current state and write a message
	if (foreground_mode == 0) {
		char message[52] = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 52);
		foreground_mode = 1;
	}
	else {
		char message[32] = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 32);
		foreground_mode = 0;
	}
}

/*Function for redirecting stdout to a file
specified by the user*/
void redirectSTDOUT(char** command, int numArgs) {
	int fd = open(command[numArgs - 1], O_WRONLY | O_CREAT | O_TRUNC, 0640);

	//Handle open() errors
	if (fd == -1) {
		printf("cannot open %s for output\n", command[2]);
		exit(1);
	}
	int result = dup2(fd, 1);

	//Handle dup2() errors
	if (result == -1) {
		perror("dup2");
		exit(2);
	}

	/*Call the execlp() function with the right number of arguments depending on
	the number of arguments the user specified*/
	if (numArgs <= 3) {
		execlp(command[0], command[0], NULL);
	}
	else if (numArgs == 4)
		execlp(command[0], command[0], command[1], NULL);
	else if (numArgs == 5)
		execlp(command[0], command[0], command[2], NULL);
	else if (numArgs == 6)
		execlp(command[0], command[0], command[1], command[3], NULL);
}


/*Function for redirecting stdin from a file
specified by the user*/
void redirectSTDIN(char** command, int numArgs, int* status) {
	int fd = open(command[2], O_RDONLY, 0640);

	//Handle open() errors
	if (fd == -1) {
		(*status) = 1;
		printf("cannot open %s for input\n", command[2]);
		exit(1);
	}
	int result = dup2(fd, 0);

	//Handle dup2() errors
	if (result == -1) {
		perror("dup2");
		exit(1);
	}

	//Call the execlp() function to run the non built-in command
	if (numArgs > 1)
		execlp(command[0], command[0], NULL);
}

/*
* Function for running the non built-in commands using fork() and the exec() family functions.
*/
void runCommand(char** command, int* status, int numArgs, int procType, int** processIDs, 
	int* numProcs, struct sigaction* SIGTSTP_act) {
	command[numArgs] = NULL; //Ensure that the last argument is NULL for the exec() functions
	pid_t spawnpid = -5; //Child process ID
	int childStatus = 0; //Child process exit status
	int validRedirect = 0; //For checking if the user command requires redirecting stdin or stdout
	spawnpid = fork(); //Fork a new child process
	switch (spawnpid) {
	//Code for if the fork fails
	case -1:
		perror("fork() failed!");
		(*status) = 1;
		exit(1);
		break;
	//Code for the child process
	case 0:
		//Handle foreground process signals
		signal(SIGTSTP, SIG_IGN);
		if (procType == 0 || foreground_mode == 1) {
			signal(SIGINT, handle_SIGINT_fg);
		}
		//Handle background process signals
		else if (procType == 1) {
			signal(SIGINT, SIG_IGN);
		}

		//Check to see if the arguments has one of the redirection operators
		for (int i = 1; i < numArgs; i++) {
			if (strcmp(command[i], ">") == 0) //Implies a redirecting of stdout
				validRedirect = 1;
			if (strcmp(command[i], "<") == 0) //Implies a redirecting of stdin
				validRedirect = 2;
		}

		//Handle the redirecting of stdout
		if (numArgs > 1 && validRedirect == 1) {
			redirectSTDOUT(command, numArgs);
		}
		//Handle the redirecting of stdin
		else if (numArgs > 1 && validRedirect == 2) {
			redirectSTDIN(command, numArgs, status);
		}

		/*Chech to see if the process is a background process to
		redirect stdin and stdout to /dev/null*/
		else if (procType == 1) {
			int fd = open("/dev/null", O_WRONLY);
			dup2(fd, 1);
			dup2(fd, 0);
			execvp(command[0], command);
		}
		/*If the command does not involve redirecting stdin or stdout, just
		call execvp() with current arguments*/
		else {
			if(foreground_mode == 1 && strcmp(command[numArgs-1], "&") == 0)
				command[numArgs-1] = NULL;
			execvp(command[0], command);
		}
		printf("%s : no such file or directory\n", command[0]);
		exit(1);
		break;

	//Code for the parent process
	default:

		//Ignore signal interruption in the parent process (shell)
		signal(SIGTSTP, handle_SIGTSTP);
		//Check if the command is for a foreground process
		if (procType == 0 || foreground_mode == 1) {
			pid_t childPid = waitpid(spawnpid, &childStatus, 0);
			if(strcmp(command[0], "test") == 0 && childStatus != 0)
				(*status) = 1;
			else
				(*status) = childStatus;
		}
		//If the command is for a background process, don't wait for child process to terminate
		else if (procType == 1) {
			signal(SIGINT, SIG_IGN);
			printf("background pid is %d\n", spawnpid);
			pid_t childPid = waitpid(spawnpid, &childStatus, WNOHANG);

			//Keep track of the process ID since the process is running in the background
			processIDs[*numProcs] = malloc(10 * sizeof(int));
			*(processIDs[*numProcs]) = spawnpid;
			(*numProcs)++;
		}
		break;
	}
}
/*Function for checking if any background process has finished,
so the child process exit status can be retrieved*/
void checkBgProcesses(int** processIDs, int numProcs) {
	for (int i = 0; i < numProcs; i++) {
		if (*(processIDs[i]) != 0) {
			int childStatus = 1;
			pid_t childPid = waitpid(*(processIDs[i]), &childStatus, WNOHANG);
			/*Check to see if a child process has terminated successfully or due to a signal
			and set it's process ID to 0 if it is terminated*/
			if (WIFEXITED(childStatus)) {
				printf("background pid %d is done : exit value %d\n", *(processIDs[i]), WEXITSTATUS(childStatus));
				*(processIDs[i]) = 0;
			}
			else if(WIFSIGNALED(childStatus) == SIGKILL){
				printf("background pid %d is done: terminated by signal %d\n", *(processIDs[i]), WTERMSIG(childStatus));
				*(processIDs[i]) = 0;
			}
			else if (WIFSIGNALED(childStatus) == SIGTERM || kill(*(processIDs[i]), 0) == -1) {
				printf("background pid %d is done: terminated by signal %d\n", *(processIDs[i]), WTERMSIG(childStatus));
				*(processIDs[i]) = 0;
			}
		}
	}
}

/*Function used for parsing the user input in a similar way to as done
in the previous two assignments. Each argument is tokenized.*/
void parseUserInput(char* postInput, char** args, int* numArgs, int* processType) {
	char* token; //String for tokenizing user input
	char* saveptr; //String used in companion to the token string

	//Tokenizes the input string into an array of strings for arguments
	for (token = strtok_r(postInput, " ", &saveptr);
		token != NULL;
		token = strtok_r(NULL, " ", &saveptr)) {
		args[*numArgs] = calloc(100, sizeof(char));
		if (strcmp(token, "&") == 0) {
			*processType = 1;
			free(args[*numArgs]);
		}
		else {
			strcpy(args[*numArgs], token);
			(*numArgs)++;
		}
	}
}

/*Function that kills any running background processes before
the shell program terminates*/
void killChildProcesses(int** processIDs, int numProcs) {
	for (int i = 0; i < numProcs; i++) {
		//Checks to see if the process had already been completed (pid == 0)
		if (*(processIDs[i]) != 0) {
			kill(*(processIDs[i]), SIGKILL);
		}
		free(processIDs[i]);
	}
	free(processIDs);
}

int main() {

	//Initializing the variables involved in the command line handling process
	char command[100]; //String for command part of user input
	char* args[512]; //Array of strings for entirety of user input
	char* token; //String for tokenizing user input
	char* saveptr; //String used in companion to the token string
	int** processIDs = malloc(200 * sizeof(int)); //Array of integers for the process IDs
	int numProcs = 0; //Integer for the number of processes running
	int status = 0; //Integer for the status of the child process terminating (for status command)

	do {
		//Initializing the three variables used for parsing each command line user input
		char* preInput = malloc(2048 * sizeof(char)); //String for user input before checking for variable expansion
		char* postInput = malloc(2048 * sizeof(char)); //String for user input after checking for variable expansion
		char* checkString = malloc(2048 * sizeof(char)); //String for checking the user input for variable expansion
		int processType = 0; //Integer to keep track of whether a process is foreground or background (0, 1)
		int numArgs = 0; //Integer for keeping track of the number of arguments in user input

		//Check for any running background processes to terminate
		checkBgProcesses(processIDs, numProcs);

		//Set the command line prompt and get user input
		printf(": ");
		fflush(stdout);
		fgets(preInput, 512, stdin);

		//Call the function for checking for variable expansion
		strcpy(checkString, "");
		strcpy(postInput, checkVariableExpansion(preInput, checkString));

		/*Parse the user input into a multi-dimensional array of strings using same method
		as last two assignments*/
		parseUserInput(postInput, args, &numArgs, &processType);

		//Ensure that the last argument has no newline character
		fflush(stdout);
		if (numArgs > 1) {
			strtok(args[numArgs - 1], "\n");
		}
		//If there is no user input, then set the first argument (command) to empty
		else if (numArgs == 0) {
			args[0] = calloc(100, sizeof(char));
			strcpy(args[0], "");
		}

		struct sigaction SIGTSTP_act;
		SIGTSTP_act.sa_handler = handle_SIGTSTP;
		sigfillset(&SIGTSTP_act.sa_mask);
		SIGTSTP_act.sa_flags = 0;
		sigaction(SIGTSTP, &SIGTSTP_act, NULL);

		signal(SIGINT, SIG_IGN);

		//Check to see if the user input is a built-in function
		if (strcmp(args[0], "cd") == 0) {
			status_flag = 0;
			fflush(stdout);
			//Check for command and a single argument
			if (numArgs > 1) {
				strtok(args[1], "\n");
				if (args[1][0] == '/') {
					memmove(args[1], args[1] + 1, strlen(args[1]));
				}
				changeDir(args[1]);
			}
			//Check for just command, indicating to go back to home directory
			else if(numArgs == 1)
				changeDir(getenv("HOME"));
		}
		//For if the user input is the status command
		else if(strcmp(args[0], "status") == 0) {
			if (status_flag == 0) {
				printf("exit value %d\n", status);
				fflush(stdout);
			}
			else if (status_flag > 0) {
				printf("terminated by signal %d\n", status_flag);
				fflush(stdout);
			}
		}
		//For if the user input is a comment
		else if (args[0][0] == '#') {
			fflush(stdout);
		}
		//For if the user input is empty
		else if (strcmp(args[0], "") == 0) {
			fflush(stdout);
		}
		//If the user input is none of the above, run it as a non built-in command
		else if(strcmp(args[0], "exit") != 0) {
			status_flag = 0;
			runCommand(args, &status, numArgs, processType, processIDs, &numProcs, &SIGTSTP_act);
		}

		//Set the command variable
		setCommand(command, args, numArgs);

		//Free memory
		freeMemory(preInput, postInput, checkString, args, numArgs);


	} while (strcmp(command, "exit") != 0); //If the user input is the exit command, break out of the loop

	//Kill any remaining child processes that are running
	killChildProcesses(processIDs, numProcs);

	return 0;
}
