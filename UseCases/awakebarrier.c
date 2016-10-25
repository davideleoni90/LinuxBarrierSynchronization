#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
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
	int id,awake,tag;
	if(argc==3){
		id = strtol(argv[1],NULL,10);
		tag= strtol(argv[2],NULL,10);
		printf("Waking up tag %d of barrier with id %d\n",tag,id);
		awake=awake_barrier(id,tag);
		if(!awake) {
                        printf("Tag %d of barrier with id %d successfully woken up\n", tag, id);
                        return 0;
                }
                else{
			switch(errno){
                                case EINVAL:{
                                        printf("Error while waking up tag %d of barrier with id %d:invalid barrier id or tag\n",tag,id);
                                        break;
                                }
                                case ENOSYS:{
                                        printf("Error while waking up tag %d of barrier with id %d: \"barrier_module\" not inserted\n",tag,id);
                                        break;
                                }
                                default:
                                        printf("Error while waking up tag %d of barrier with id %d:%d\n",tag,id,errno);
                        }
                        return errno;
		}
        }
	else
		printf("Invalid arguments: only provide valid barrier ID and synchronization tag\n");
}
