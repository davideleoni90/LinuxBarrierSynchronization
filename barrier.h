#ifndef BARRIERSYNCHRONIZATION_BARRIER_H
#define BARRIERSYNCHRONIZATION_BARRIER_H

/*
 * The possible flags that can be used when
 * a new barrier is requested:
 *
 * BARRIER_CREATE: the barrier has to be created if it doesn't exist
 * BARRIER_EXCL: an error code has to be returned if BARRIER_CREATE is invoked and the barrier already exists
 *
 */

#include <linux/ipc_namespace.h>

#define BARRIER_CREATE 00001000
#define BARRIER_EXCL 00002000

/*
 * The number of PRIORITY SYNCHRONIZATION TAGS: possible values for these
 * tags go from 0 to BARRIER_TAGS-1
 */

#define BARRIER_TAGS 32

/*
 * Structure to handle all the instances of the barrier, particularly
 * their ids
 */

struct ipc_ids barrier_ids;

/*
 * Kernel service routine to get the an instance of a barrier.
 * Paremeters:
 * 1- key_t key: used to univokely identify the instance of the barrier
 * 2- int flags: flags to define the action to perform on the barrier
 */

asmlinkage long sys_get_barrier(key_t key,int flags);

/*
 * Initialize the "ids_namespace" structure to be used to handle ids of the
 * varieous instances of the barrier
 */

extern void ipc_init_ids(struct ipc_ids *);

/*
 * Function to add the id of a new instance of barrier to the set of barrier
 * ids of type ipc_ids
 */

extern int ipc_addid(struct ipc_ids *, struct kern_ipc_perm *, int);

/*
 * Structure representing a new barrier: the most important field is the array
 * with 32 elements, one per PRIORITY SYNCHRONIZATION TAG, where each element
 * is a list of pointers to wait queues for that specific TAG, one per process
 * which requested to sleep on the barrier with that priority tag. The wait queue
 * is stored in the kernel mode stack of the process, because we don't want to
 * allocate more memory than what is already allocated by the stack. Of course
 * we have to keep the level of stack occupancy low, but since we have one wait
 * queue for each synchronization tag, and each wait queue has only one element,
 * we occupy at most 32*size_of(wait_queue) for each barrier
 */

struct barrier_struct{

        /*
         * kern_perm_struct corresponding to the barrier: this is used to
         * deal with usage permissions in the IPC API
         */

        struct kern_ipc_perm	barrier_perm;

        /*
         * Array of lists of pointers to wait queues: the number
         * of elements in the array amounts to BARRIER_TAGS
         */

        struct list_head  [BARRIER_TAGS] queues;



        /*
         *
         */

        //time_t			sem_otime;	/* last semop time */
        //time_t			sem_ctime;	/* last change time */
        //struct sem		*sem_base;	/* ptr to first semaphore in array */
        //struct list_head	sem_pending;	/* pending operations to be processed */
        //struct list_head	list_id;	/* undo requests on this array */
        //int			sem_nsems;	/* no. of semaphores in array */
        //int			complex_count;	/* pending complex operations */
};

/*
 * Structure that holds some ipc operations. This structure is used to unify
 * the calls to sys_msgget(), sys_semget(), sys_shmget()
 *      . routine to call to create a new ipc object. Can be one of newque,
 *        newary, newseg
 *      . routine to call to check permissions for a new ipc object.
 *        Can be one of security_msg_associate, security_sem_associate,
 *        security_shm_associate
 *      . routine to call for an extra check if needed
 */
struct ipc_ops {
        int (*getnew) (struct ipc_namespace *, struct ipc_params *);
        int (*associate) (struct kern_ipc_perm *, int);
        int (*more_checks) (struct kern_ipc_perm *, struct ipc_params *);
};


#endif //BARRIERSYNCHRONIZATION_BARRIER_H
