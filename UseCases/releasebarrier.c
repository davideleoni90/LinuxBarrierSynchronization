#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/ipc.h>
#include <linux/ipc.h>
#include <errno.h>
#include "barrier_user.h"

int get_barrier(key_t key, int flags){
	return syscall(nr_get_barrier,key,flags);
}

int sleep_on_barrier(int bd, int tag){
	return syscall(nr_sleep_on_barrier,bd,tag);
}

int awake_barrier(int bd, int tag){
	return syscall(nr_awake_barrier,bd,tag);
}

int release_barrier(int md){
	return syscall(nr_release_barrier,md);
}


int main(int argc, char** argv){
	int id,release;
	if(argc==2){
		id = strtol(argv[1],NULL,10);
		printf("Releasing barrier with id %d\n",id);
		release=release_barrier(id);
		if(!release)
			printf("Barrier with id %d successfully released\n",id);
		else{
			switch(errno){
				case EINVAL:{
					printf("Error while releasing barrier with id %d:invalid barrier id or tag\n",id);
					break;
				}
				case ENOSYS:{
					printf("Error while releasing barrier with id %d: \"barrier_module\" not inserted\n",id);
					break;
				}
				default:
					printf("Error while barrier with id %d:%d\n",id,errno);
			}
			return errno;
		}
	}
	else
		printf("Invalid argument: provide only valid barrier ID\n");
}
