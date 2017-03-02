#include <stdio.h>
#include <errno.h>
#include "arch/x86/include/generated/uapi/asm/unistd_64.h"
#include <stdlib.h>

int main(int argc, char ** argv)
{
	char *msg = "Hej!";
	syscall(__NR_msg_put, msg, 5);

	msg = "Kage!";
	syscall(__NR_msg_put, msg, 6);

	char *get = (char *)malloc(6 * sizeof(char));
	syscall(__NR_msg_get, get, 6);
	printf("Msg: %s\n", get);

	free(get);
	get = (char *)malloc(5 * sizeof(char));
	syscall(__NR_msg_get, get, 5);
	printf("Msg: %s\n", get);
}
