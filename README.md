# queued
A really simple job queue utility for linux written in C.

The goal is to process jobs on a network of workstations **(NOW)** sharing a filesystem.
Jobs are run either imediately returning stdout and stderr to the user or queued for execution when sufficient resources are available.

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
Submitting a job to the job queue creates a job description and sends that job description to the master queue server.  That server will send the job to an execution server running on a workstation capable of running the job.  After the job is run either the job description is deleted or update with job status. Often a job will be submitted with a foreach loop or a script.
For example to transcode a video,

```
% q -e group=linux ffmpeg -i LostInTranslation.mkv -codec copy LostInTranslation.mp4
jobid: jobid uid gid
sch 1
```

option:
  `-e` specified enqueue the job rather than run it now

parms:
  `group` specifies a host or group of hosts capable of running the job
  `keep=error` directs the system to leave the job description in place after an error (return code != 0) Could use `always` instead.
  `mem=10G` lets the scheduler know the job requires 10GB of memory to execute
  `threads=10` lets the scheduler know the job requires 10 threads to execute

### Job descriptions
Job descriptions are stored in ~/.queued as directories using jobid is the directory name.
The command line and the parms as well as stdout and stderr are stored there.  
This requires that ~ is in a shared filesystem and is accessable to the submitting machine, the master machine and the execution machine with the same pathname.

### Listing the Queue
The current list of jobs that are not yet complete can be queried. 
Generally, a user is limited to viewing the list of thier own jobs and is not given access to the jobs of other users.

```
% q -l
username jobhost 2025/10/29@12:04:50 0 jobid command line given to q -e
```

Listing of the users jobs (one line per job) in submission order, either currently running or waiting to run.

#### Job List Fields
  `username` is the unix username of submitter.
  `jobhost` is the hostname of host running the job or `-` if job is waiting to run.
  `date@time` is the date and time the job was submitted.
  `0` is the job group the user used to submit the job.
  `jobid` is the jobs jobid.
  `command` is the command line submitted.
  
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

  `jobhost` is the hostname of the host.
  `24/16` is the total number of threads and cores available in the host.
  `mips` is the linux kernel bogo mips rating.
  `lavg` are the three columns of load average *100.
  `xproc` are the number of processes, running processes and threads running on the machine external to the queue.
  `q` are the number of processes, running processes, threads and jobs running on the machine from the queue.
  `mem` are the total memory, used memory and the available memory in GB.
  `x` is the virtual memory used by processes external to the queue.
  `q` is the virtual memory used by jobs running from the queue.
  `users` is the number of users logged in to the host and the number of seconds since a command has been typed.
  `up` is the uptime since boot, the current time and the seconds of drift.

#### Job Status Fields

  `-` this is a job status line.
  `username` user who submitted the job.
  `jobid` is the jobid for the job.
  `3 1 3 4` are the number of processes, number of running processes and number of threads running and the virtual size of the job
  
  `command` the command line submitted to the queue

### Removing Jobs from the Queue
Jobs can be removed from the queue, before or while running.  If the job is running it will be terminated,

```
% q -d jobid
dequeue of jobid suceeded
```


