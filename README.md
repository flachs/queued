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
Job descriptions are stored in ~/.queued as directories unique to the job.  The command line and the parms as well as stdout and stderr are stored there.  This assumes that ~ is in a shared filesystem and is accessable to the submitting machine, the master machine and the execution machine with the same pathname.



 
