#ifndef PROCPS_PROC_READPROC_H
#define PROCPS_PROC_READPROC_H

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#define KLONG long
#define P_G_SZ 33

enum ns_type {
    IPCNS = 0,
    MNTNS,
    NETNS,
    PIDNS,
    USERNS,
    UTSNS,
    NUM_NS         // total namespaces (fencepost)
};


#define PROCPATHLEN 64  // must hold /proc/2000222000/task/2000222000/cmdline

#define PROC_FILLMEM         0x0001 // read statm
#define PROC_FILLCOM         0x0002 // alloc and fill in `cmdline'
#define PROC_FILLENV         0x0004 // alloc and fill in `environ'
#define PROC_FILLUSR         0x0008 // resolve user id number -> user name
#define PROC_FILLGRP         0x0010 // resolve group id number -> group name
#define PROC_FILLSTATUS      0x0020 // read status
#define PROC_FILLSTAT        0x0040 // read stat
#define PROC_FILLARG         0x0100 // alloc and fill in `cmdline'
#define PROC_FILLCGROUP      0x0200 // alloc and fill in `cgroup`
#define PROC_FILLSUPGRP      0x0400 // resolve supplementary group id -> group name
#define PROC_FILLOOM         0x0800 // fill in proc_t oom_score and oom_adj
#define PROC_FILLNS          0x8000 // fill in proc_t namespace information
#define PROC_FILLSYSTEMD    0x80000 // fill in proc_t systemd information
#define PROC_FILL_LXC      0x800000 // fill in proc_t lxcname, if possible

#define PROC_LOOSE_TASKS     0x2000 // treat threads as if they were processes

// consider only processes with one of the passed:
#define PROC_PID             0x1000  // process id numbers ( 0   terminated)
#define PROC_UID             0x4000  // user id numbers    ( length needed )

#define PROC_EDITCGRPCVT    0x10000 // edit `cgroup' as single vector
#define PROC_EDITCMDLCVT    0x20000 // edit `cmdline' as single vector
#define PROC_EDITENVRCVT    0x40000 // edit `environ' as single vector

// it helps to give app code a few spare bits
#define PROC_SPARE_1     0x01000000
#define PROC_SPARE_2     0x02000000
#define PROC_SPARE_3     0x04000000
#define PROC_SPARE_4     0x08000000



typedef struct proc_pid_status_s
  {
  // 1st 16 bytes
  int
  tid,            // (special)       task id, the POSIX thread ID (see also: tgid)
    ppid;           // stat,status     pid of parent process
  unsigned long       // next 2 fields are NOT filled in by readproc
  maj_delta,      // stat (special) major page faults since last update
    min_delta;      // stat (special) minor page faults since last update
  unsigned
  pcpu;           // stat (special)  %CPU usage (is not filled in by readproc!!!)
  char
  state,          // stat,status     single-char code for process state (S=sleeping)
#ifdef QUICK_THREADS
    pad_1,          // n/a             padding (psst, also used if multi-threaded)
#else
    pad_1,          // n/a             padding
#endif
    pad_2,          // n/a             padding
    pad_3;          // n/a             padding
// 2nd 16 bytes
  unsigned long long
  utime,          // stat            user-mode CPU time accumulated by process
    stime,          // stat            kernel-mode CPU time accumulated by process
  // and so on...
    cutime,         // stat            cumulative utime of process and reaped children
    cstime,         // stat            cumulative stime of process and reaped children
    start_time;     // stat            start time of process -- seconds since system boot
#ifdef SIGNAL_STRING
  char
  // Linux 2.1.7x and up have 64 signals. Allow 64, plus '\0' and padding.
  signal[18],     // status          mask of pending signals, per-task for readtask() but per-proc for readproc()
    blocked[18],    // status          mask of blocked signals
    sigignore[18],  // status          mask of ignored signals
    sigcatch[18],   // status          mask of caught  signals
    _sigpnd[18];    // status          mask of PER TASK pending signals
#else
  long long
  // Linux 2.1.7x and up have 64 signals.
  signal,         // status          mask of pending signals, per-task for readtask() but per-proc for readproc()
    blocked,        // status          mask of blocked signals
    sigignore,      // status          mask of ignored signals
    sigcatch,       // status          mask of caught  signals
    _sigpnd;        // status          mask of PER TASK pending signals
#endif
  unsigned KLONG
  start_code,     // stat            address of beginning of code segment
    end_code,       // stat            address of end of code segment
    start_stack,    // stat            address of the bottom of stack for the process
    kstk_esp,       // stat            kernel stack pointer
    kstk_eip,       // stat            kernel instruction pointer
    wchan;          // stat (special)  address of kernel wait channel proc is sleeping in
  long
  priority,       // stat            kernel scheduling priority
    nice,           // stat            standard unix nice level of process
    rss,            // stat            identical to 'resident'
    alarm,          // stat            ?
  // the next 7 members come from /proc/#/statm
    size,           // statm           total virtual memory (as # pages)
    resident,       // statm           resident non-swapped memory (as # pages)
    share,          // statm           shared (mmap'd) memory (as # pages)
    trs,            // statm           text (exe) resident set (as # pages)
    lrs,            // statm           library resident set (always 0 w/ 2.6)
    drs,            // statm           data+stack resident set (as # pages)
    dt;             // statm           dirty pages (always 0 w/ 2.6)
  unsigned long
  vm_size,        // status          equals 'size' (as kb)
    vm_lock,        // status          locked pages (as kb)
    vm_rss,         // status          equals 'rss' and/or 'resident' (as kb)
    vm_rss_anon,    // status          the 'anonymous' portion of vm_rss (as kb)
    vm_rss_file,    // status          the 'file-backed' portion of vm_rss (as kb)
    vm_rss_shared,  // status          the 'shared' portion of vm_rss (as kb)
    vm_data,        // status          data only size (as kb)
    vm_stack,       // status          stack only size (as kb)
    vm_swap,        // status          based on linux-2.6.34 "swap ents" (as kb)
    vm_exe,         // status          equals 'trs' (as kb)
    vm_lib,         // status          total, not just used, library pages (as kb)
    rtprio,         // stat            real-time priority
    sched,          // stat            scheduling class
    vsize,          // stat            number of pages of virtual memory ...
    rss_rlim,       // stat            resident set size limit?
    flags,          // stat            kernel flags for the process
    min_flt,        // stat            number of minor page faults since process start
    maj_flt,        // stat            number of major page faults since process start
    cmin_flt,       // stat            cumulative min_flt of process and child processes
    cmaj_flt;       // stat            cumulative maj_flt of process and child processes
  char
  **environ,      // (special)       environment string vector (/proc/#/environ)
    **cmdline,      // (special)       command line string vector (/proc/#/cmdline)
    **cgroup,       // (special)       cgroup string vector (/proc/#/cgroup)
    *cgname,       // (special)       name portion of above (if possible)
    *supgid,       // status          supplementary gids as comma delimited str
    *supgrp;       // supp grp names as comma delimited str, derived from supgid
  char
  // Be compatible: Digital allows 16 and NT allows 14 ???
  euser[P_G_SZ],  // stat(),status   effective user name
    ruser[P_G_SZ],  // status          real user name
    suser[P_G_SZ],  // status          saved user name
    fuser[P_G_SZ],  // status          filesystem user name
    rgroup[P_G_SZ], // status          real group name
    egroup[P_G_SZ], // status          effective group name
    sgroup[P_G_SZ], // status          saved group name
    fgroup[P_G_SZ], // status          filesystem group name
    cmd[64];        // stat,status     basename of executable file in call to exec(2)
  struct proc_t
  *ring,          // n/a             thread group ring
    *next;          // n/a             various library uses
  int
  pgrp,           // stat            process group id
    session,        // stat            session id
    nlwp,           // stat,status     number of threads, or 0 if no clue
    tgid,           // (special)       thread group ID, the POSIX PID (see also: tid)
    tty,            // stat            full device number of controlling terminal
  /* FIXME: int uids & gids should be uid_t or gid_t from pwd.h */
    euid, egid,     // stat(),status   effective
    ruid, rgid,     // status          real
    suid, sgid,     // status          saved
    fuid, fgid,     // status          fs (used for file access only)
    tpgid,          // stat            terminal process group id
    exit_signal,    // stat            might not be SIGCHLD
    processor;      // stat            current (or most recent?) CPU
  int
  oom_score,      // oom_score       (badness for OOM killer)
    oom_adj;        // oom_adj         (adjustment to OOM score)
  long
  ns[NUM_NS];     // (ns subdir)     inode number of namespaces
  char
  *sd_mach,       // n/a             systemd vm/container name
    *sd_ouid,       // n/a             systemd session owner uid
    *sd_seat,       // n/a             systemd login session seat
    *sd_sess,       // n/a             systemd login session id
    *sd_slice,      // n/a             systemd slice unit
    *sd_unit,       // n/a             systemd system unit id
    *sd_uunit;      // n/a             systemd user unit id
  const char
  *lxcname;       // n/a             lxc container name
  } proc_pid_status_t;


typedef struct proc_pid_stat_s
  {
  char cmd[64];
  int   state;   // single-char code for process state (S=sleeping)
  pid_t ppid;    // pid of parent process
  pid_t pgrp;    // process group id
  int   session; // session id
  int   tty;     // full device number of controlling terminal
  int   tpgid;   // terminal process group id
  unsigned long flags;   // kernel flags for the process
  unsigned long min_flt; // number of minor page faults since process start
  unsigned long maj_flt; // number of major page faults since process start
  unsigned long cmin_flt;// cumulative min_flt of process and child processes
  unsigned long cmaj_flt;// cumulative maj_flt of process and child processes
  unsigned long long utime; // user-mode CPU time accumulated by process
  unsigned long long stime; // kernel-mode CPU time accumulated by process
  unsigned long long cutime;// cumulative utime of process and reaped children
  unsigned long long cstime;// cumulative stime of process and reaped children
  int priority; // kernel scheduling priority
  int nice;     // standard unix nice level of process
  int nlwp;     // number of threads, or 0 if no clue
  long alarm;
  unsigned long long start_time; // start time of process -- seconds since system boot
  unsigned long long vsize;      // number of pages of virtual memory ...
  long rss;            // identical to 'resident'
  unsigned long rss_rlim;       // resident set size limit?
  unsigned KLONG start_code;    // address of beginning of code segment
  unsigned long end_code;       // address of end of code segment
  unsigned long start_stack;    // address of the bottom of stack for the process
  unsigned long kstk_esp;       // kernel stack pointer
  unsigned long kstk_eip;       // kernel instruction pointer
  unsigned long wchan;          // address of kernel wait channel proc is sleeping in
  int exit_signal;    // might not be SIGCHLD
  int processor;      // current (or most recent?) CPU
  unsigned long rtprio; // real-time priority
  unsigned long sched;  // scheduling class
  unsigned long long delayacct_blkio_ticks;  // Aggregated block I/O delays, measured in clock ticks (centiseconds).
  unsigned long guest_time; // time spent running a virtual CPU, measured in clock ticks (divide by sysconf(_SC_CLK_TCK)).
  long cguest_time; // Guest time of the process's  children, measured in clock ticks
  unsigned long start_data; // Address above which program initialized and uninitialized (BSS) data are placed.
  unsigned long end_data; // Address below which program initialized and uninitialized (BSS) data are placed.
  unsigned long start_brk; // Address above which program heap can be expanded with brk(2).
  unsigned long arg_start; // Address above which program command-line arguments (argv) are placed.
  unsigned long arg_end;   // Address below program command-line arguments (argv) are placed.
  unsigned long env_start; // Address above which program environment is placed.
  unsigned long env_end;   // Address below which program environment is placed.
  int exit_code;
  } proc_pid_stat_t;

int read_proc_stat(int pid, proc_pid_stat_t *restrict P);
typedef int (recieve_proc_entry_handler_t)(void *arg,int pid,
                                           proc_pid_stat_t *procp);
void read_proc_table(recieve_proc_entry_handler_t *handler,
                    void *handlerarg);


#endif



















































