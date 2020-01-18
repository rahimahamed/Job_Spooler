#ifndef MOREFUNC_H
#define MOREFUNC_H

typedef struct job {
    JOB_STATUS status;
    char* taskspec;
    TASK task;
    int exit_status;
    int pgid;
    int occupied;
} job;

extern struct job *jobs_table;

extern int number_of_jobs;

extern int enabled_flag;

extern int number_of_runner_processes;

extern int runner_freed_flag;

void sigchld_handler(int sig);

int sig_chld_handler_func();

#endif