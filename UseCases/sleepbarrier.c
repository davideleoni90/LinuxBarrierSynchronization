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
        int id,sleep,tag,i;
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_sigaction = sighandler;
        act.sa_flags = SA_SIGINFO;
        for(i=1;i<32;i++){
                sigaction(i, &act, NULL);
        }
        if(argc==3){
                id = strtol(argv[1],NULL,10);
                tag = strtol(argv[2],NULL,10);
                printf("PID of current process:%d\n",getpid());
                printf("Now go to sleep on barrier with id %d on tag %d\n",id,tag);
                sleep=sleep_on_barrier(id,tag);
                if(sleep<0) {
                        switch(errno){
                                case EINTR:{
                                        printf("Process woken up because of interrupt\n");
                                        break;
                                }
                                case EINVAL:{
                                        printf("Error while going to sleep on tag %d of barrier with id %d: invalid barrier id or tag\n",tag,id);
                                        break;
                                }
                                case ENOSYS:{
                                        printf("Error while going to sleep on tag %d of barrier with id %d: \"barrier_module\" not inserted\n",tag,id);
                                        break;
                                }
                                default:
                                        printf("Could not sleep on tag %d of barrier with id %d because of error:%d\n",tag,id,errno);
                        }
                        return errno;
                }
                printf("Process woken up by another process\n");
                return 0;
        }
        printf("Invalid arguments: provide barrier id as first parameter and tag as second parameter\n");
}
