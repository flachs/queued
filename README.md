# queued
A really simple job queue utility for linux written in C.

The goal is to process jobs on a network of workstations **(NOW)** sharing a filesystem.
Jobs are run either imediately returning stdout and stderr to the user or queued for execution when sufficient resources are available.

# Usage Information

## Immediate Execution
The runnow mode is a `rsh` splat utility function that is useful for maintaining a NOW.  The command is run sequentially on all specified hosts using an context very similar to the user's currently active environment and current working directory.
For example,

```
% q -n host=linux date
mach1: Wed Oct 29 09:03:01 CDT 2025
mach2: Wed Oct 29 09:03:01 CDT 2025
mach3: Wed Oct 29 09:03:01 CDT 2025
mach4: Wed Oct 29 09:03:01 CDT 2025
```

options:
  `-n` prefix output with the hostname to make the report easier to understand
  `-sin` uses the same stdin text for all runs.

parms:
  `host` specifies either a hostname or machine group to run the command on. The default is all.

## Queued Execution
Submitting a job to the job queue creates a job description and sends that job description to the master queue server.
The master will send the job to an execution server running on a workstation capable of running the job.
The job will be sent for execution either immediately upon submission or at the earliest time that it is the highest priority job that could schedule.

Priority order is:

  1. the user running the least number of jobs,
  2. the user that least recently had a job scheduled,
  3. the pri parm (0 first .. 7 last), and
  4. submisstion time with the oldest job first.
  
However, higher priority jobs that cannot schedule do not block lower priority jobs that can schedule.
Users cannot use pri parm to advance their jobs before another user's jobs.

After the job is run, either the job description is deleted or update with job status.
Often a job will be submitted with a foreach loop or a script.
For example to transcode a video,

```
% q -e group=linux ffmpeg -i LostInTranslation.mkv -codec copy LostInTranslation.mp4
jobid: jobid uid gid
sch 1
```

option:

  * `-e` specified enqueue the job rather than run it now
  
parms:

  * `jg=0`  integer job group, for matching in list and dequeue.
  * `pri=4` scheduling piority with 0 being first and 7 last.
  * `group` specifies a host or group of hosts capable of running the job
  * `keep=error` directs the system to leave the job description in place after an error (return code != 0) Could use `always` instead.
  * `mem=10G` lets the scheduler know the job requires 10GB of memory to execute
  * `threads=10` lets the scheduler know the job requires 10 threads to execute

output:
  * `jobid` returns the created jobid
  * `sch`   returns 1 if the job ran immediately or 0 if it is queued
  * 'rej'   returns the reason the job was rejected

### Job descriptions
Job descriptions are stored in ~/.queued as directories using
jobid is the directory name.
The command line and the parms as well as stdout and stderr are
stored there.  
This requires that ~ is in a shared filesystem and is accessable
to the submitting machine, the master machine and the execution
machine with the same pathname.

### Listing the Queue
The current list of jobs that are not yet complete can be queried. 
Generally, a users other than root are limited to viewing the list
of thier own jobs and are not shown jobs queued by other users.

```
% q -l
username jobhost 2025/10/29@12:04:50 0 jobid command line given to q -e
```

Listing of the users jobs (one line per job) in submission order,
either currently running or waiting to run.

#### Job List Selection
The job list function utilizes parms to control which jobs are
included in the list.

parms:

  * `time=beg,end` selects jobs submitted between beg and end.
    If end is omitted, then the range ends now.
  * `cmd=regexp` selects jobs whose command lines match the
    regular expression.
  * `host=hostname` selects jobs that are running on a host.
  * `jg=integer` selects jobs that belong to the job group.
  * `pri=integer` selects jobs that were submitted with a priority.
  * `a=all`       selects all jobs.
  * `u=uname`     selects the jobs submitted by a user or '-' for all users.
  * `jobid`       selects a job by its jobid.

#### Job List Fields
  * `username` is the unix username of submitter.
  * `jobhost` is the hostname of host running the job
              or `-` if job is waiting to run.
  * `date@time` is the date and time the job was submitted.
  * `0` is the job group the user used to submit the job.
  * `jobid` is the jobs jobid.
  * `command` is the command line submitted.
  
### NOW Status
The status of the machines in the NOW can be queried.

```
% q -s show=jobs hostgroup
jobhost: 24/16cores 4224mips lavg: 300 276 198 xproc 64/1/99 q 6/2/6 2 mem 125gb used 87 avail 45 x 59gb q 33gb users 1 6sec up 5w,2d,45:12 0s-drift
 - username jobid 3 1 3 4 command line given to q -q
```
A report with one section per host is produced. The first line of a host section is host status. 
If show=jobs is requested, one line per job running on the host is follows the host status.

#### Host Status Fields

  * `jobhost` is the hostname of the host.
  * `24/16` is the total number of threads and cores available in the host.
  * `mips` is the linux kernel bogo mips rating.
  * `lavg` are the three columns of load average *100.
  * `xproc` are the number of processes, running processes and threads running on the machine external to the queue.
  * `q` are the number of processes, running processes, threads and jobs running on the machine from the queue.
  * `mem` are the total memory, used memory, available memory and the memory used for buffers and cache in GB.
  * `xv` is the virtual memory used by processes external to the queue.
  * `xr` is the resident size of processes external to the queue.
  * `q` is the virtual memory used by jobs running from the queue.
  * `users` provides the number of users logged in to the host who have typed a command in the last 30 mins and the number of seconds since a command has been typed.
  * `up` is the uptime since boot, the current time and the seconds of drift.

#### Job Status Fields

  * `-` this is a job status line.
  * `username` user who submitted the job.
  * `jobid` is the jobid for the job.
  * `3 1 3 4` are the number of processes, number of running processes and number of threads running and the virtual size of the job
  * 
  * `command` the command line submitted to the queue

### Removing Jobs from the Queue
Jobs can be removed from the queue, before or while running.  If the job is running it will be terminated,

```
% q -d jobid
dequeue of jobid suceeded
```

All of the selection logic available for `-l` are available also for `-d`.
For complex job dequeuing, use `-l` to develop the critereon for `-d`.

# Building Queued
The q binary is a set-uid root process and is used both for the user client interface and for the server daemons.
The `makefile` includes the install target that copies and ug+s.
You will need to configure the INSTALL_DIR for your system.  
```
% cd src
% make
% sudo make install
```

Some systems running systemd might benefit from the queued.service example in the source directory.

# Configuration
When q starts, it reads the configuration file `/etc/queued.conf`. 
The source directory contains an example file.

## Pid Directory
There must be a directory in a shared filesystem to store the pid files created by the sever daemons.
The pid files will be created when the daemon starts and contain the server key.
The files are owned by root read/writeable only by root.  
The client needs the key inorder to send tcp messages to the daemon. 
Daemons also need the keys for inter-server communications.

```
dir pid = /user/utility/packages/queued/pids;
```
Specifies the path to the directory where the pid files will be created.
It should be created owned by root:root with mode 755.

## Machine groups
Machine groups are used to restrict the job execution to particular machines.
The `master` group is manditory and specifies which machine will be the master scheduler.
There can be only one machine in the master group.

```
group master = mist;
```

The group `all` is also manditory and is used as the default for all operations.
```
group all = mist smoke asst luke;
```
The all group does not need to have all of the machines.  For example, I dont have
the laptops because they arent always connected to the NOW.

You can create any other groupings that make sense.
```
group name = list of machines
```

## Machine limits
You can limit the available resources for each machine.  
Jobs wont schedule if they dont fit the available resources.

```
limits machine = mem=16G buf=20g threads=8 busy=8:00-19:00;
```

## Tokens
You can create token pools to manage resources such as licenses.
Jobs must be able to claim the specified tokens before they can be schedule, 
and they hold thier tokens until they exit.

```
token LIC = 2;
```


