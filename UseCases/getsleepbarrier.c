#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/ipc.h>
#include <errno.h>
#include "barrier_user.h"
#include <signal.h>

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

void sighandler(int signum, siginfo_t *info, void *ptr){
        printf("Received signal %d\n", signum);
        printf("Signal originates from process %lu\n",(unsigned long)info->si_pid);
}


int main(int argc, char** argv){
	int id,sleep,key,flags,tag,i;
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_sigaction = sighandler;
        act.sa_flags = SA_SIGINFO;
        for(i=1;i<32;i++){
                sigaction(i, &act, NULL);
        }
        if(argc>2){
		key = strtol(argv[1],NULL,10);
                tag = strtol(argv[2],NULL,10);
                flags=0;
                if(argc==4)
		        flags = strtol(argv[3],NULL,10);
                printf("PID of current process:%d\n",getpid());
                printf("Find barrier with key %d and flags %d\n",key,flags);
		id=get_barrier(key,flags);
                if(id<0) {
                        switch(errno){
                                case ENOMEM: {
                                        printf("Error while getting barrier:not enough memory available\n");
                                        break;
                                }
                                case ENOSPC:{
                                        printf("Error while getting barrier:too many barriers already instantiated\n");
                                        break;
                                }
                                case ENOSYS:{
                                        printf("Error while getting barrier: \"barrier_module\" not inserted\n");
                                        break;
                                }
                                case ENOENT:{
                                        printf("Error while getting barrier: no barrier with key %d found\n",key);
                                        break;
                                }
                                case EEXIST:{
                                        printf("Error while getting barrier: barrier with key %d already exists\n",key);
                                        break;
                                }
                                default:
                                        printf("Error while getting barrier:%d\n",errno);
                        }
                        return errno;
                }
                printf("Now go to sleep on barrier with id %d on tag %d\n",id,tag);
		sleep=sleep_on_barrier(id,tag);
		if(sleep<0) {
                        switch(errno){
                                case EINTR:{
                                        printf("Process woken up because of interrupt\n");
                                        break;
                                }
                                case EINVAL:{
                                        printf("Error while going to sleep on barrier: invalid barrier id or tag\n");
                                        break;
                                }
                                default:
                                        printf("Could not sleep because of error:%d\n",errno);
                        }
                        return errno;
                }
                printf("Process woken up by another process\n");
                return 0;
        }
        printf("Invalid arguments: at least provide barrier key as first parameter and tag as second parameter;\n"
                               "optionally provide flags as third parameter\n");
}
