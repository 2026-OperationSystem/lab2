#include "types.h"
#include "user.h"

int main(int argc, char *argv[])
{
	int pid, child_pid;
	char *message;
	int n, status, exit_code;

	pid = fork();
        switch(pid)	{
		case -1:
			printf(2,"fork failed");
			exit2(0);
		case 0:
			message = "This is the child";
			n = 5;
			exit_code = 37;
			break;
		default:
			message = "This is the parent";
			n = 3;
			exit_code = 0;
			break;
	}
	for(; n > 0; n--) {
		printf(1, "%s\n", message);
		sleep(1);
	}

	if (pid != 0) {
		child_pid = wait2(&status);
		printf(1, "Child has finished: PID = %d\n", child_pid);
		printf(1, "Child exited with code %d\n", status);
	}
	exit2(exit_code);
}