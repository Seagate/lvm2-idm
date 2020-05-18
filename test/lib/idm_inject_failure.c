#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <uuid/uuid.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ilm.h>

int main(void)
{
	int ret, s;

	ret = ilm_connect(&s);
	if (ret == 0) {
		printf("ilm_connect: SUCCESS\n");
	} else {
		printf("ilm_connect: FAIL\n");
		exit(-1);
	}

	ret = ilm_inject_fault(s, 100);
	if (ret == 0) {
		printf("ilm_inject_fault (100): SUCCESS\n");
	} else {
		printf("ilm_inject_fault (100): FAIL\n");
		exit(-1);
	}

	ret = ilm_disconnect(s);
	if (ret == 0) {
		printf("ilm_disconnect: SUCCESS\n");
	} else {
		printf("ilm_disconnect: FAIL\n");
		exit(-1);
	}

	return 0;
}
