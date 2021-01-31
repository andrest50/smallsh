To compile the program, run:

	gcc --std=gnu99 -o smallsh main.c

To run the executable produced by the command above, run:

	./smallsh

Features:

	Built-in commands:
	-exit
	-cd
	-status

	Other features:
	-Redirection of standard input and output
	-Ability to execute other commands using exec() functions
	-Run background processes
	-Signal with Crtl-C (interrupt) and Crtl-Z (terminate)
	-Use variable $$ to replace with process ID