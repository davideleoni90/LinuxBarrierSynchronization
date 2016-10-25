# LinuxBarrierSynchronization
<p align="justify">
This application is the result of a didactic project for the
<a href="http://www.dis.uniroma1.it/~quaglia/DIDATTICA/SO-II-6CRM/">
Operating Systems 2</a> course of the Master of Science of
Engineering in Computer Science at <a href="http://cclii.dis.uniroma1.it/?q=it/msecs">Sapienza University of Rome</a>.
The author of this project is <a href="https://www.linkedin.com/in/leonidavide">Davide Leoni</a>
</p>
<h2>Overview</h2>
<p align="justify">
This project is aimed at the creation of a new synchronization system for the Linux Kernel: in order to introduce this new facility into the kernel, a dedicated module has been developed.
</p>
<h2>Specifics</h2>
<p align="justify">
The new synchronization system resembles a <a href="https://en.wikipedia.org/wiki/Barrier_%28computer_science%29">synchronization barrier</a> but is also capable of handling <i> 32 synchronization TAGs</i> (from 0 to 31), thus it is possible to synchronize groups of processes with different level of priorities instantiating a single barrier. Similarly to any barrier, processes are synchronized by putting them to sleep until a certain event happens or a condition is verified: here processes wake up when an explicit request is made by another process. It is possible to select the processes to wake up by mean of the TAG so that only the process which were synchronized on the corresponding priority level get back to normal execution. Moreover, in case a process receives an interrupt, it has to recognize that it has been waken up not because of synchronization needs (a process requested to wake up all the processes with its TAG), but because the operating system had to notify him an event.
<br>
The interface of the new synchronization system (namely the new system calls that have to be provided by the kernel) is the following:
<ol type="1">
<li><b>int get_barrier(key_t key, int flags)</b>: get the barrier corresponding to the given <i>key</i>; the provided <i>flags</i> are the same used for I/O operations, for example when a file has to be opened. The value returned is the unique ID associated to the barrier and has to be used to perform futher operations on it</li>
<li><b>int sleep_on_barrier(int bd, int tag)</b>: the calling process now synchronizes with processes of the group <i>tag</i> sleeping on the barrier with ID <i>bd</i></li>
<li><b>int awake_barrier(int bd, int tag)</b>: all the processes belonging to group <i>tag</i> sleeping on the barrier with ID <i>bd</i> are waken up by the calling process</li>
<li><b>int release_barrier(int md)</b>: remove barrier with ID <i>bd</i> from the system</li>
</ol>
</p>
<h2>Implementation</h2>
<p align="justify">
Every time a process has to be synchronized on a barrier at a certain priority level, a <i>wait-queue</i> is allocated in its own Kernel mode stack and it goes to sleep on it; the address of the wait-queue is stored in a list whose head is kept in the data structure associated to the barrier, so that it is possible to properly awake the process when the <i>awake_barrier</i> system call is invoked. This solution exploits the memory of the Kernel mode stack, which is statically allocated to a process, thus avoiding to request memory dynamically to the operating system for each synchronization tag (besides the memory necessary to store the data structure associated to the barrier, containing a minimal list of addresses of wait queues). As a consequence, better memory usage and scalability are achieved.
<br>
In order to provide a robust handling of the IDs associated to the barriers, this module makes use of many functions natively used by the Linux Kernel for the IPC subsystem (see <i>"How to use"</i>). This comes at the price of finding the addresses of a few more kernel functions before compiling the module.
<br>
The usage counter of the module is incremented every time a new instance of barrier is successfully created: this prevents the module from being removed while some barriers are still in place. As a consequence, <b>it is necessary to release all the allocated barriers otherwise
it won't be possible to remove the module (and a system restart will be necessary)</b>
</p>
<h2>How to use</h2>
<p align="justify">
The module relies on a few kernel functions and symbols which are not always exported: their address has to be taken from the file <i>System.map</i>.
<br>
Here's the list of symbols, from header file <i>helper.h</i>:
<ol type="1">
<li><b>not_implemented_syscall</b> (corresponding to <i>sys_ni_syscall</i> in the System.map)</li>
<li><b>ipc_init_ids</b></li>
<li><b>ipcget</b></li>
<li><b>ipc_rcu_alloc</b></li>
<li><b>ipc_addid</b></li>
<li><b>ipc_rmid</b></li>
<li><b>ipc_rcu_putref</b></li>
<li><b>ipc_lock_check</b></li>
</ol>
<br>
Once this symbols have been initialised to their correct address, the module can be compiled and installed as any other module for the Linux Kernel.
The module was tested on Linux Kernel 2.6.34, with full preemption and SMP support, on x86 machine
<br>
The folder <i>UseCases</i> features some examples of usage of the module. Before using them, it is necessary to insert the compiled module into the kernel and then set the number of newly installed system calls into the header <i>barrier_user.h</i> (after the module has been inserted, the number of the newly installed system calls can be read from the kernel log using the command <i>dmesg</i>).
</p>
