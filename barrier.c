#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <asm/unistd.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/ipc.h>
#include <linux/idr.h>
#include <linux/gfp.h>
#include <linux/rwsem.h>
#include <linux/sem.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/ipc_namespace.h>
#include <asm-generic/current.h>
#include <asm-generic/pgtable.h>
#include "barrier.h"
#include "helper.h"

/*
 * Pointer to the ipc_ids data structure used to keep track of  all the instances
 * of the barrier on the basis of their ids
 */

struct ipc_ids* barrier_ids;

/*
 * Functions to check permission over the barrier: we don't care
 * about permissions, so always return 0
 *
 * @ipcp: permission object to which is function is associated
 * @flag: parameters to this function
 */

int barrier_security(struct kern_ipc_perm * ipcp, int flag){
        return 0;
}

/*
 * Unlock the permission object within a barrier: this means unlocking the
 * permission object and also end the RCU read-side critical section
 *
 * @barrier: pointer to the barrier whose permission object has to be unlocked
 */

void barrier_unlock(struct barrier_struct* barrier){
        struct kern_ipc_perm* barrier_ipc_perm=&(barrier->barrier_perm);
        spin_unlock(&(barrier_ipc_perm->lock));
        rcu_read_unlock();
}

/*
 * Dinamically create a "barrier_tag" element for the given tag
 *
 * @tag: tag associated to the structure
 *
 * Returns the address of the barrier_tag structure or NULL
 * in case there's not enough memory for the new object
 */

struct barrier_tag* newtag(int tag){

        /*
         * Pointer to the new barrier_tag instance
         */

        struct barrier_tag* new_tag;

        /*
         * Request memory for the new object
         */

        new_tag=kmalloc(sizeof(struct barrier_tag),GFP_KERNEL);

        printk(KERN_INFO "Address of the tag number %d:%lu\n",tag,new_tag);

        /*
         * Check if the barrier_tag object has been successfully allocated: if not,
         * return the error -ENOMEM (no memory space)
         */

        if(!new_tag)
                return ERR_PTR(-ENOMEM);

        /*
         * Initialize the new "barrier_tag" object:
         *
         * 1- set the "counter" field to 0
         * 2- set the tag field
         * 3- initialize the list of pointers to wait queues of sleeping processes
         * 4- set the boolean "sleeping" to true
         */

        new_tag->counter=0;
        new_tag->tag=tag;
        INIT_LIST_HEAD(&(new_tag->queues));
        new_tag->sleeping=false;

        /*
         * Return the initialized barrier tag
         */

        return new_tag;
}

/*
 * Callback function invoked by the IPC subsystem when a new barrier has to be created
 *
 * @ns: ipc_namespace to be considered; actually we don't care of this parameter, it is
 *      given just because the IPC subsystem requires uses it to handle its default
 *      synchronization objects (semaphores,shared memories and message queues)
 * @params: flags and key associated to the new barrier
 *
 * Returns the IPC identifier of the newly created barrier or some error code in case
 * something goes wrong.
 *
 * This function is called holding the mutex of the global (shared among all barriers)
 * "ipc_ids" structure as writer
 *
 */

int newbarrier(struct ipc_namespace* ns, struct ipc_params* params){

        /*
         * The unique IPC identifier of the new barrier; recall that
         * this id is unique within a "data structure type scope",i.e.
         * there may be an IPC message queue or IPC semaphore with
         * the same id as this barrier we are creating
         */

        int id;

        /*
         * The basic structure for the barrier we are creating
         */

        struct barrier_struct *barrier;

        /*
         * The key requested by the user-space process
         */

        key_t key = params->key;

        /*
         * The flags requested by the user-space process
         */

        int barrierflags = params->flg;

        /*
         * Allocate the memory for the barrier: the barrier_structure is preceded by
         * an RCU header which is necessary to protect our barrier during RCU read-side
         * critical sections and then to update it after these critical sections are
         * over in case some other process meanwhile (concurrently) updated them
         */

        barrier = ipc_rcu_alloc(sizeof(*barrier));

        printk(KERN_INFO "Address of the barrier with key %d:%lu\n",key,barrier);

        /*
         * Return error in case there's not enough memory left
         * for the barrier
         */

        if (!barrier) {
                return -ENOMEM;
        }

        /*
         * Set the key of the kern_ipc_perm  within the new barrier
         */

        barrier->barrier_perm.key = key;

        /*
         * Set the mode (flag) of the kern_ipc_perm  within the new barrier:
         * we make a bitwise AND with the flag "S_IRWXUGO", which grants
         * read,write and execute permissions to user, group and others
         */

        barrier->barrier_perm.mode=barrierflags & S_IRWXUGO;

        /*
         * Set to NULL the "security" field of the kern_ipc_perm structure,
         * since this is only used in SELinux
         */

        barrier->barrier_perm.security=NULL;

        /*
         * Get a new id for the newly created barrier instance.
         * The permission object (kern_ipc_perm) of the barrier is initialized
         * and locked.
         *
         * The id returned is the one assigned by the IDR object within the ipc_ids
         * structure but the actual IPC identifier is written into the permission
         * object and is equal to
         *
         * IPC_id=SEQ_MULTIPLIER * seq + id;
         *
         * where "seq" is the sequence number of the barrier, "id" is the identifier
         * returned by IDR and SEQ_MULTIPLIER is a constant equal to IPCMNI (32768)
         *
         * The third parameter determines the limit for the number of ids used for the
         * barrier: in the IPC subsystem this parameter is taken from the ipc_namespace,
         * we define a specific constant for it
         */

        id = ipc_addid(barrier_ids,&barrier->barrier_perm,BARRIER_IDS_MAX);

        /*
         * If the id returned is negative, this is a sign that something went wrong,
         * so the barrier object has to be removed, together with the rcu_head structure
         * associated -> the function below decreases the counter of references to the
         * RCU-protected barrier structure (the counter is stored within the rcu header
         * prepended to the barrier structure) and then checks if this counter is null,
         * i.e. if no other process has references to this data structure: if that's the
         * case, the "call_rcu" function from the RCU API is invoked, so the structure will
         * be removed as soon as all the RCU read-side critical sections connected to it
         * will be over
         *
         * The error code is then returned.
         */

        if(id<0){
                ipc_rcu_putref(barrier);
                return id;
        }

        /*
         * Initialize the head of the list of "barrier_tag" objects
         */

        INIT_LIST_HEAD(&(barrier->tags));

        /*
         * The new instance of barrier is complete, so we can unlock it to make
         * it accessible to all the other processes
         */

        barrier_unlock(barrier);

        /*
         * Return the assigned IPC identifier
         */

        return barrier->barrier_perm.id;
}

/*
 * Return a pointer to the "barrier_tag" structure corresponding to the given tag within the given
 * barrier; returns NULL if this is not found
 *
 * @barrier: permission object of the barrrier where the tag has to be searched
 * @tag: tag to search for
 *
 * This has to be called only after the tag value has been verified to be valid, i.e. 0<=tag<=31, so here
 * we don't need further checks
 *
 * Also this has to be called holding the lock on barrier
 *
 */

struct barrier_tag* findtag(struct barrier_struct* barrier,int tag){

        /*
         * Pointer to the barrier_tag structure corresponding to the given tag
         */

        struct barrier_tag* out;

        /*
         * Scan the whole list until the "barrier_struct" corresponding to
         * the given tag is found
         */

        list_for_each_entry(out,&barrier->tags,tag_list)
                if(out->tag==tag)
                        return out;

        return NULL;
}

/*
 * Get the permission object of the barrier associated to the provided IPC
 * identifier; also check the provided tag is valid.
 *
 * The permission object, whether found, is retuerned in a locked state
 *
 * This
 *
 * @bd: IPC identifier of the barrier that the process wants to use in order to synchronize
 *      with other processes
 * @tag: index of specific queue of the barrier onto which the process wants to sleep
 *
 * Returns:
 *
 * 1- the permission object requested, if one corresponding to the given IPC identifier exists
 * 2- -EINVAL in case the requested tag is not valid or no object corresponding to the IPC
 * identifier exists
 */

struct kern_ipc_perm* checkbarrier(int tag,int bd){

        /*
         * The return value
         */

        struct kern_ipc_perm* ret;

        /*
         * Check if the provided tag is valid (less than 0<=tag<=31):
         * if not, return -EINVAL
         */

        if(tag<0 || tag >31) {
                ret=ERR_PTR(-EINVAL);
                return ret;
        }

        /*
         * Check if a permission object associated to the provided IPC identifier exists and, if so,
         * return it in a locked state, otherwise return error code -EINVAL.
         */

        ret=ipc_lock_check(barrier_ids, bd);

        /*
         * Return the output of the function
         */

        return ret;
}

/*
 * This function wakes up all the processes sleeping on the synchronization level corresponding
 * to the given barrier_tag structure and then removes the last one from the associated list
 * of the barrier object
 *
 * Function has to be invoked holding the lock on the barrier object containing the tags
 *
 * @barrier_tag: structure representing the tag to be woken up
 *
 * Returns nothing
 */

void awake_tag(struct barrier_tag* barrier_tag){

        printk(KERN_INFO "Waking up tag:%d\n",barrier_tag->tag);

        /*
         * Pointer to an element of the list associated to the barrier_tag
         */

        struct process_queue* tag_list_element;

        /*
         * Head of wait queue in process
         */

        wait_queue_head_t* head;

        /*
         * It is necessary to set the value of the field "sleeping" (from the barrier_tag structure)
         * to "true" and then  call the "wake_up" function on each of the wait queues stored in the list "queues".
         *
         * In this way, as soon as the sleeping processes wake up, they realize that the sleeping
         * condition no longer holds, so they go back to the TASK_RUNNING state and are removed from
         * the wait queue
         */

        barrier_tag->sleeping=true;

        /*
         * Scan the list of wait queues head and wake the single process sleeping on each of them
         * The "wake_up" function iterates through all the elements of the wait queue until they
         * have all woken up. In particular it invokes the "autoremove_wake_function" on each sleeping
         * process: this function returns 1 when the process has woken up, 0 otherwise, so the "awaker"
         * keeps trying to wake up processes until on all of them the "wake up" function has returned
         * 1
         */

        list_for_each_entry(tag_list_element,&barrier_tag->queues,queue_list) {
                head=&tag_list_element->queue;
                if(!head->task_list.next)
                        printk(KERN_INFO "Empty Entry address:%lu\n",head->task_list.next);
                else
                        wake_up(tag_list_element->queue);
        }

        printk(KERN_INFO "Woken up tag:%d\n",barrier_tag->tag);

        /*
         * Remove the structure associated to this tag from the corresponding list of the barrier
         */

        list_del(&barrier_tag->tag_list);

        printk(KERN_INFO "Removed tag structure from list of tags:%d\n",barrier_tag->tag);

        /*
         * Once we exit the above loop, we can be sure that all the processes synchronized on the current
         * tag have been woken up => now we can remove the structure corresponding to the tag
         */

        kfree(barrier_tag);

        printk(KERN_INFO "Removed tag structure:%d\n",barrier_tag->tag);
}

/*
 * Remove the barrier object associated to the given permission object:
 *
 * 1-awake all processes sleeping on the barrier
 * 2-remove all the wait queues in the kernel mode stack of the processes that were
 *   sleeping on the barrier
 * 3-remove the corresponding entry from the IDR object of barrier_ids
 *
 * @perm: permission object of the barrier to be removed
 *
 * This function is called holding the mutex of the ipc_ids structure for barriers and
 * the lock of the permission object of the barrier to be released
 */

void freebarrier(struct kern_ipc_perm* perm){

        /*
         * Pointer to barrier to be removed
         */

        struct barrier_struct* to_be_removed;

        /*
         * Structure representing the single tag whose processes has to be woken up
         */

        struct barrier_tag* tag;

        /*
         * Temporary pointer used inside "list_for_each_entry_safe"
         */

        struct barrier_tag* temp;

        /*
         * Get the barrier corresponding to the given permission object
         */

        to_be_removed=container_of(perm,struct barrier_struct,barrier_perm);

        printk(KERN_INFO "Releasing barrier with id %d at address %lu\n",perm->id,to_be_removed);

        /*
         * Wake up processes sleeping on each tag and release objects associated to the
         * tags themselves
         *
         * Inside the loop we are going to delete barrier_tag structures (after processes sleeping on them
         * have been woken up) so we need the "safe" version of "list_for_each_entry", which makes use of
         * a futher pointer (second parameter) to save the address of the entry that is gonna be deleted
         */

        list_for_each_entry_safe(tag,temp,&to_be_removed->tags,tag_list)
                awake_tag(tag);

        /*
         * Stop the association between the IPC identifier (provided by the idr of the
         * ipc_ids data structure) and the permission object of the barrier: now it's
         * no longer reachable using the IPC identifier
         */

        printk(KERN_INFO "Before removing id %d from idr\n",perm->id);

        ipc_rmid(barrier_ids,perm);

        printk(KERN_INFO "Removed id %d from idr\n",perm->id);

        /*
         * Check if removed: we expect we can't find the entry in the idr
         */

        void* prova=tag;

        prova=idr_find(&barrier_ids->ipcs_idr,(perm->id) % IPCMNI);

        if(!prova)
                printk(KERN_INFO "Released id of barrier with id %d\n",perm->id);
        else
                printk(KERN_INFO "Couldn't find id %d\n",perm->id);

        /*
         * Release the lock on the permission object
         */

        barrier_unlock(to_be_removed);

        printk(KERN_INFO "Unlocked barrier with id %d\n",perm->id);

        /*
         * Free memory assigned to the barrier
         */

        ipc_rcu_putref(to_be_removed);

        printk(KERN_INFO "Removed barrier with id %d\n",perm->id);
}

/*
 * Callback function invoked on each pointer registered in the idr object when
 * it is necessary to iterate through them
 *
 * @id: id associated to the pointer
 * @p: the pointer itself
 * @data: further data for the function
 *
 * Returns 0 in case of success, -1 otherwise
 */

int idr_iterate_callback (int id, void *p, void *data){

        /*
         * Pointer to the current permission object registered in the idr
         */

        struct kern_ipc_perm* perm;

        /*
         * Cast the pointer received as second parameter
         */

        perm=(struct kern_ipc_perm*)p;

        printk(KERN_INFO "Bbarrier id :%d\n",perm->id);

        /*
         * Acquire the lock on the permission object pointed by p
         */

        rcu_read_lock();
        spin_lock(&perm->lock);

        /*
         * Release the barrier associated to the permission object
         */

        freebarrier(perm);

        /*
         * It has to be successful, so return 0
         */

        return 0;
}

/*
 * Remove the ipc_ids structure associated to the barriers
 *
 * First it is necessary to acquire the mutex of the ipc_ids as writer and then remove
 * all the instance of barriers, each corresponding to an IPC identifier stored in the
 * idr object.
 *
 * As soon as there are no more instances of barrier in the system, it is possible to
 * remove the ipc_ids structure and free memory
 *
 * @barrier_ids: the ipc_ids structure of the barriers
 */

void remove_ids(void){

        /*
         * Acquire the mutex on the ipc_ids structure of the barriers because we are going
         * to remove all the entries from its IDR object
         */

        down_write(&barrier_ids->rw_mutex);

        /*
         * Iterate through all permission objects, one for each barrier, registered with the
         * idr of the ipc_ids structure: for each of them, release the corresponding barrier
         *
         * The function below, from the IDR API, iterates through all the registered pointers
         * and applies to them the callback function given as second parameter; the third
         * parameter represents further data that can be passed to the callback function, but
         * we don't need it
         */

        idr_for_each(&barrier_ids->ipcs_idr,idr_iterate_callback,NULL);

        printk(KERN_INFO "All barriers removed\n");

        /*
         * Once the above function has finished its task, there are no more instances of barrier
         * around, so we can unlock the ipc_ids and then free it
         */

        up_write(&barrier_ids->rw_mutex);
        kfree(barrier_ids);
}

/*
 * SYSTEM CALL KERNEL SERVICE ROUTINES - start
 *
 * 1 - sys_get_barrier
 * 2 - sys_release_barrier
 * 3 - sys_sleep_on_barrier
 * 4 - sys_awake_barrier
 */

/*
 * Put the current process to sleep on barrier with given IPC identifier on the queue
 * corresponding to the given tag
 *
 * @bd: IPC identifier of the barrier that the process wants to use in order to synchronize
 *      with other processes
 * @tag: index of specific queue of the barrier onto which the process wants to sleep
 *
 * Returns an error code in case something went wrong, -ERESTARTSYS in case the process has
 * been woken up because it has received a signal, 0 otherwise
 */

asmlinkage long sys_sleep_on_barrier(int bd,int tag){

        /*
         * Return value of this system call
         */

        int ret;

        /*
         * Permission object associated to the given IPC identifier (if valid)
         */

        struct kern_ipc_perm* barrier_perm;

        /*
         * Barrier associated to the given IPC identifier (if valid)
         */

        struct barrier_struct* barrier;

        /*
         * Structure corresponding to the given tag within the barrier associated
         * to the given IPC identifier (if exists)
         */

        struct barrier_tag* barrier_tag;

        /*
         * Structure representing the current process in the "queues" list (within
         * the "barrier_tag" structure)
         */

        struct process_queue process_queue;

        printk(KERN_INFO "System call sys_sleep_on_barrier invoked with params: barrier descriptor=%d tag=%d\n",bd,tag);

        /*
         * Check if the provided tag is valid (less than 0<=tag<=31):
         * if not, return -EINVAL
         */

        if(tag<0 || tag >31) {
                printk(KERN_INFO "System call sys_sleep_on_barrier returned this value:%d\n",ret);
                ret=-EINVAL;
                return ret;
        }

        /*
         * Check if a permission object associated to the provided IPC identifier exists and, if so,
         * return it in a locked state, otherwise return error code -EINVAL.
         *
         * TO BE CHECKED: SHOULDN'T WE ACQUIRE THE READ SEMAPHORE OF IPC IDS? WHAT IF AN ID IS REMOVED
         * WHILE GETTING THE ID?
         */

        barrier_perm=ipc_lock_check(barrier_ids, bd);

        /*
         * In case an error code is returned, i.e. no barrier corresponding to the provided id is found,
         * unlock the ipc_ids structure and return the error
         */

        if(IS_ERR(barrier_perm)){
                ret=PTR_ERR(barrier_perm);
                printk(KERN_INFO "System call sys_sleep_on_barrier returned this value:%d\n",ret);
                return ret;
        }

        /*
         * Get the instance of a barrier from its permission object using the "container_of" macro:
         * given a pointer to a member of a structure and the type of the structure itself, it is
         * possible to get the address of the specific instance of the structure to which given member
         * is part of
         */

        barrier=container_of(barrier_perm,struct barrier_struct,barrier_perm);

        /*
         * Check if a "barrier_tag" for the given tag exists in this barrier; if not, we have to allocate
         * one
         */

        barrier_tag=findtag(barrier,tag);
        if(!barrier_tag){

                printk(KERN_INFO "Creating struct barrier_tag for tag:%d\n",tag);

                /*
                 * Allocate a new barrier_tag to handle the synchronization tag
                 */

                barrier_tag=newtag(tag);

                /*
                 * If the allocation was not successful, return the error -ENOMEM and
                 * release the lock on the permission object
                 */

                if(IS_ERR(barrier_tag)){
                        barrier_unlock(barrier);
                        ret=PTR_ERR(barrier_tag);
                        printk(KERN_INFO "System call sys_sleep_on_barrier returned this value:%d\n",ret);
                        return ret;

                }

                printk(KERN_INFO "Adding tag %d to list\n",tag);

                /*
                 * Add the newly created barrier_tag to the list of tags from the
                 * barrier
                 */

                list_add(&barrier_tag->tag_list,&barrier->tags);

                printk(KERN_INFO "Added tag %d to list\n",tag);
        }

        /*
         * Check if the limit of sleeping processes for the given tag has been reached:if so, return
         * -ENOSPC (no space left) error code and unlock the permission object
         */

        if(barrier_tag->counter==BARRIER_PER_TAG_MAX){
                barrier_unlock(barrier);
                ret=-ENOSPC;
                printk(KERN_INFO "System call sys_sleep_on_barrier returned this value:%d\n",ret);
                return ret;
        }

        /*
         * Declare and initialize the head of the wait queue onto which the process is
         * going to sleep: since this is a local variable, it's gonna be allocated into
         * the Kernel Mode Stack of the calling process.
         */

        DECLARE_WAIT_QUEUE_HEAD(queue_head);

        /*
         * Set the instance representing the current process in the "queues" list of the barrier_tag
         * structure
         */

        process_queue.queue=&queue_head;

        /*
         * Add the new element representing the current process within the "queues" list to this one
         */

        list_add(&process_queue.queue_list,&barrier_tag->queues);

        printk(KERN_INFO "Adding process to list of tag %d: the address is %lu\n",tag,process_queue);

        /*
         * Increment the counter of the "barrier_tag" structure because this process is now sleeping
         * on this tag
         */

        barrier_tag->counter++;

        /*
         * Release the lock on the permission object
         */

        barrier_unlock(barrier);

        /*
         * Put the current process to sleep on its own wait queue: it is woken up when the sleeping
         * variable of the barrier_tag structure is set to "true" by another process
         *
         * Also we want the process to exit from the wait queue when an interrupt comes, so we
         * use the "interruptible version" of the wait_event function
         *
         * The return value is -ERESTARTSYS if a signal came to wake up the process, 0 if it
         * is waken up because the sleeping condition evaluated to true
         */

        ret=wait_event_interruptible(queue_head,barrier_tag->sleeping);

        /*
         * Return the result of the preceding function as the result of the entire system call
         */

        printk(KERN_INFO "System call sys_sleep_on_barrier returned this value:%d\n",ret);
        return ret;
        }

/*
 * Wake up all the processes synchronized on a certain tag of the barrier corresponding to the
 * given IPC identifier
 *
 * @bd: IPC identifier of the barrier that the process wants to use in order to synchronize
 *      with other processes
 * @tag: index of specific queue of the barrier onto which the process wants to sleep
 *
 * Returns:
 */

asmlinkage long sys_awake_barrier(int bd,int tag){

        /*
         * Return value of this system call
         */

        int ret;

        /*
         * Permission object associated to the given IPC identifier (if valid)
         */

        struct kern_ipc_perm* barrier_perm;

        /*
         * Barrier associated to the given IPC identifier (if valid)
         */

        struct barrier_struct* barrier;

        /*
         * Structure corresponding to the given tag within the barrier associated
         * to the given IPC identifier (if exists)
         */

        struct barrier_tag* barrier_tag;

        printk(KERN_INFO "System call sys_awake_barrier invoked with params: barrier descriptor=%d tag=%d\n",bd,tag);

        /*
         * Check if the provided tag is valid (less than 0<=tag<=31):
         * if not, return -EINVAL
         */

        if(tag<0 || tag >31) {
                printk(KERN_INFO "System call sys_awake_barrier returned this value:%d\n",ret);
                ret=-EINVAL;
                return ret;
        }

        /*
         * Check if a permission object associated to the provided IPC identifier exists and, if so,
         * return it in a locked state, otherwise return error code -EINVAL.
         */

        barrier_perm=ipc_lock_check(barrier_ids, bd);

        /*
         * In case an error code is returned, i.e. no barrier corresponding to the provided id is found,
         * unlock the ipc_ids structure and return the error
         */

        if(IS_ERR(barrier_perm)){
                ret=PTR_ERR(barrier_perm);
                printk(KERN_INFO "System call sys_awake_barrier returned this value:%d\n",ret);
                return ret;
        }

        /*
         * Get the instance of a barrier from its permission object using the "container_of" macro:
         * given a pointer to a member of a structure and the type of the structure itself, it is
         * possible to get the address of the specific instance of the structure to which given member
         * is part of
         */

        barrier=container_of(barrier_perm,struct barrier_struct,barrier_perm);

        /*
         * Check if a "barrier_tag" for the given tag exists in this barrier; if not, return error code
         * -EINVAL,
         */

        barrier_tag=findtag(barrier,tag);
        if(!barrier_tag) {

                /*
                 * No permission object corresponding to the requested tag exists, so return -EINVAL and
                 * release the lock on permission object
                 */

                barrier_unlock(barrier);
                printk(KERN_INFO "System call sys_awake_barrier returned this value:%d\n",-EINVAL);
                return -EINVAL;
        }

        /*
         * Wake up all the processes sleeping on the given tag and then remove the associated
         * object
         */

        awake_tag(barrier_tag);

        /*
         * Release the lock on the permission object
         */

        barrier_unlock(barrier);

        /*
         * The awakening was successful, so return 0
         */

        return 0;
}

/*
 * Instantiate a new barrier synchronization object
 *
 * @ key: this value is given in order to ask for a certain barrier; if the
 * "IPC_PRIVATE" key is specified, a new barrier is created
 * @flags: flags that determine how the barrier should be created or returned,
 * if it exists. They are:
 *
 * 1)"IPC_CREAT": in case no barrier corresponding to the provided key exists, a new barrier is created
 * 2)"IPC_EXCL" | "IPC_CREAT": in case no barrier corresponding to the provided key exists, a new barrier
 * is created, otherwise error "-EEXIST" is returned
 *
 * Returns:
 *
 * 1)-ENOMEM, in case the limit for the number of barriers that can be allocated has been reached
 * or if there's not enough memory to allocate a new barrier
 * 2)-ENOSPC, in case the number of barriers in use at the same time has been reached or if no more
 * IDs can be allocated by the IDR subsystem
 * 3)The unique IPC identifier (id) of the instance of barrier corresponding to the key provided or
 * the id of a new instance in case the key is IPC_PRIVATE or the key is not found and the flag "IPC_CREAT"
 * but not flag "IPC_EXCL" are specified
 */

asmlinkage long sys_get_barrier(key_t key,int flags){

        /*
         * Return value of this system call
         */

        int ret;

        /*
         * Parameters to be passed to the "ipcget" function
         */

        struct ipc_namespace *ns;
        struct ipc_ops ops;
        struct ipc_params params;

        /*
         * Use ipc_namespace of the current process
         */

        ns = current->nsproxy->ipc_ns;

        /*
         * Function associated to the creation of a new barrier
         */

        ops.getnew = newbarrier;

        /*
         * We don't care about checking permissions for our barriers,
         * so the function associated to the operation "associate"
         * simply returns 0 and "more_checks" is set to NULL
         *
         * The former has to be set because it is used in the function
         * "ipc_check_perms"
         */

        ops.associate =barrier_security;
        ops.more_checks = NULL;

        /*
         * The two parameters to
         */

        params.key = key;
        params.flg = flags;

        printk(KERN_INFO "System call sys_get_barrier invoked with params: key=%d flags=%d\n",key,flags);

        /*
         * Get a new barrier synchronization object using the above parameters
         */

        ret=ipcget(ns, barrier_ids, &ops, &params);

        printk(KERN_INFO "System call sys_get_barrier returned this value:%d\n",ret);

        /*
         * Return the result of the system call
         */

        return ret;


}

/*
 * Release a barrier synchronization instance given its IPC identifier
 *
 * @bd: IPC identifier of the barrier to be removed
 *
 * Returns a code indicating whether the operation was successful or not
 */

asmlinkage long sys_release_barrier(int bd){

        /*
         * Return value of this system call
         */

        int ret;

        /*
         * User id of the current owner of the barrier to be removed
         */

        uid_t euid;

        /*
         * The permission object of the barrier to be removed
         */

        struct kern_ipc_perm* barrier_perm;

        printk(KERN_INFO "System call sys_release_barrier invoked with params: barrier descriptor=%d\n",bd);

        /*
         * Acquire the mutex on the ipc_ids structure of the barriers because we are going
         * to remove an entry from its IDR object
         */

        down_write(&barrier_ids->rw_mutex);

        /*
         * Check if a permission object corresponding to the provided IPC identifier exists and, if so,
         * return it in a locked state, otherwise return error code -EINVAL.
         */

        barrier_perm=ipc_lock_check(barrier_ids, bd);

        /*
         * In case an error code is returned, i.e. no barrier corresponding to the provided id is found,
         * unlock the ipc_ids structure and return the error
         */

        if(IS_ERR(barrier_perm)){
                up_write(&barrier_ids->rw_mutex);
                ret=PTR_ERR(barrier_perm);
                printk(KERN_INFO "System call sys_release_barrier returned this value:%d\n",ret);
                return ret;
        }

        freebarrier(barrier_perm);

        /*
         * Release the mutex of the ipc_ids structure
         */

        up_write(&barrier_ids->rw_mutex );

        /*
         * The function was successful, so return 0
         */

        ret=0;


        printk(KERN_INFO "System call sys_release_barrier returned this value:%d\n",ret);

        /*
         * Return 0
         */

        return ret;
}

/*
 * SYSTEM CALL KERNEL SERVICE ROUTINES - end
 */

/*
 * INSERT/REMOVE MODULE - start
 */

/*
 * INSERT MODULE
 * "Dynamically" add system calls to the system:
 * -> replace not implemented system calls (sys_ni_syscall) with custom system calls
 */

int init_module(void) {

        /*
         * Value stored in the register CR0
         */

        unsigned long cr0;

        /*
         * Find the address of the system call table
         */

        system_call_table=find_system_call_table();

        /*
         * Get the indexes of the free entries in the system call table,
         * i.e. those initialized to the address of "sys_ni_syscall".
         * This is necessary in order to restore the system call table
         * when the module is removed
         */

        find_free_syscalls(system_call_table,restore);

        /*
         * In case the CPU has WRITE-PROTECTED MODE enabled, even kernel
         * thread can't modify read-only pages, such those containing the
         * system call table => we first have to disable the WRITE-PROTECTED MODE
         * by clearing the corresponding bit (number 16) in the CR0 register.
         */

        disable_write_protected_mode(&cr0);

        /*
         * Replace system call "sys_ni_syscall" in the system call table
         * with our custom system calls: we assume that no other process
         * running on any other CPU ever tries to access the system call
         * not implemented => if this assumption holds, the following code
         * is thread-safe
         */

        system_call_table[restore[0]]=(unsigned long)sys_get_barrier;
        system_call_table[restore[1]]=(unsigned long)sys_sleep_on_barrier;
        system_call_table[restore[2]]=(unsigned long)sys_awake_barrier;
        system_call_table[restore[3]]=(unsigned long)sys_release_barrier;

        /*
         * Restore original value of register CR0
         */

        enable_write_protected_mode(&cr0);

        /*
         * Allocate a new structure "ipc_ids" in order to keep track of
         * all the existing barrier objects as they are created
         */

        barrier_ids=kmalloc(sizeof(struct ipc_ids), GFP_KERNEL);

        /*
         * Initialize the newly created "ipc_ids" structure:
         *
         * 1-counter of ids in use is set to 0
         * 2-sequence number is set to 0
         * 3-mutex is initialized
         * 4-idr subsystem (to manage ids) is initialized using the function
         * "idr_init", which is part of its API
         */

        ipc_init_ids(barrier_ids);

        printk(KERN_INFO "Address of the ipc_ids:%lu\n",barrier_ids);

        printk(KERN_INFO "First field of the ipc_ids:%d\n",*barrier_ids);

        printk(KERN_INFO "Second field of the ipc_ids:%d\n",*(barrier_ids+4*sizeof(void*)));

        /*
         * Log message about our just inserted module
         */

        printk(KERN_INFO "Module \"barrier_module\" inserted: index of replaced system calls:%d,%d,%d,%d\n",restore[0],restore[1],restore[2],restore[3]);
        return 0;

}

/*
 * REMOVE MODULE
 * Restore the system call table to its original form removing our custom system  calls
 */

void cleanup_module(void) {

        unsigned long cr0;

        /*
         * In order to restore the system call table, the WRITE-PROTECTED
         * MODE has to be disabled again temporarily, so first save the
         * value of the CR0 register
         */

        disable_write_protected_mode(&cr0);

        /*
         * Restore system call table to its original shape
         */

        system_call_table[restore[0]]=not_implemented_syscall;
        system_call_table[restore[1]]=not_implemented_syscall;
        system_call_table[restore[2]]=not_implemented_syscall;
        system_call_table[restore[3]]=not_implemented_syscall;

        /*
         * Restore value of register CR0
         */

        enable_write_protected_mode(&cr0);

        /*
         * Remove the ipc_ids structure associated to the barriers
         */

        remove_ids();

        printk(KERN_INFO "First field of the removed ipc_ids:%d\n",*barrier_ids);

        printk(KERN_INFO "Second field of the removed ipc_ids:%d\n",*(barrier_ids+4*sizeof(void*)));

        printk(KERN_INFO "Released memory allocated for the structure ipc_ids at address %lu\n",barrier_ids);

        printk(KERN_INFO "Module \"barrier_module\" removed\n");

}

/*
 * INSERT/REMOVE MODULE - end
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Davide Leoni");
