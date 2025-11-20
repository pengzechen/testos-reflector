/**
 * System Call Definitions
 * 
 * Defines system call numbers and interfaces for user programs
 */

#ifndef T_SYSCALL_H
#define T_SYSCALL_H

#include "t_types.h"

/* System Call Numbers */
#define SYS_ioctl          29   // ioctl (I/O control)
#define SYS_read           63   // read from file descriptor
#define SYS_write          64   // write to file descriptor
#define SYS_writev         66   // write vector
#define SYS_exit           93   // exit process
#define SYS_exit_group     94   // exit all threads
#define SYS_getitimer      102  // get interval timer
#define SYS_tkill          130  // send signal to a thread
#define SYS_rt_sigaction   134  // signal action
#define SYS_rt_sigprocmask 135  // change signal mask
#define SYS_brk            214  // change data segment size

#define SYS_mmap     222  // map memory
#define SYS_munmap   215  // unmap memory (free mmap region)
#define SYS_mprotect 226  // change memory protection

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* System call handler */
uint64_t
handle_syscall(uint64_t syscall_num,
               uint64_t arg0,
               uint64_t arg1,
               uint64_t arg2,
               uint64_t arg3,
               uint64_t arg4,
               uint64_t arg5);

#endif /* T_SYSCALL_H */
