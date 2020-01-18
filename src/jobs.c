/*
 * Job manager for "jobber".
 */

#include <stdlib.h>

#include "jobber.h"
#include "task.h"
#include "sf_readline.h"
#include "moreFunc.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int enabled_flag;
int runner_freed_flag;
int number_of_jobs;
int number_of_runner_processes;
struct job *jobs_table;

int jobs_init(void) {

    //INSTALL SIG HANDLER
    signal(SIGCHLD, sigchld_handler);
    signal(SIGSTOP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    signal(SIGKILL, SIG_DFL);
    sf_set_readline_signal_hook(sig_chld_handler_func);

    number_of_jobs = 0;
    enabled_flag = 0;
    number_of_runner_processes = 0;
    runner_freed_flag = 0;

    jobs_table = malloc(sizeof(struct job) * MAX_JOBS);
    return 0;
}

void jobs_fini(void) {
    if(number_of_jobs == 0){
        free(jobs_table);
        exit(EXIT_SUCCESS);
    }
    for(int i = 0; i < MAX_JOBS; i++){
        if(jobs_table[i].occupied == 1){
            if(jobs_table[i].status != 5 || jobs_table[i].status != 5)
                job_cancel(i);
            job_expunge(i);
        }
    }
    free(jobs_table);
    exit(EXIT_SUCCESS);
}

int jobs_set_enabled(int val) {
    int prev_flag = enabled_flag;
    if(val != 0)
        enabled_flag = 1;
    else
        enabled_flag = 0;

    //IF THERE IS WAITING JOBS START THEM
    for(int i = 0; i< MAX_JOBS; i++){
        if((jobs_table[i].status == 1) && (jobs_table[i].occupied == 1) && (number_of_runner_processes < 4)){
            jobs_table[i].status = 2;
            sf_job_status_change(i, 1, 2);

            //CREATE RUNNER PROCESS
            number_of_runner_processes++;
            int child_status;
            pid_t pid_runner;

            //BLOCK SIGCHLD SIGNAL
            sigset_t mask, prev;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &mask, &prev);

            if((pid_runner = fork()) == 0){
                setpgid(0,0);
                sf_job_start(i, getpgrp());
                printf("Runner Process, PID = %d, PGID = %d\n", getpid(), getpgrp());

                //GET COMMAND LIST THAT CHANGES AFTER EVERY PIPELINE
                TASK task = jobs_table[i].task;
                PIPELINE_LIST pipe_to_execute = *task.pipelines;
                COMMAND_LIST cmd_list = *pipe_to_execute.first->commands;

                //EXECUTE ALL PIPELINES SEQEUNTIALLY
                while(1){

                    //CREATE COMMANDS ARRAY TO EXECUTE CONCURRENTLY
                    COMMAND commands[100];
                    int i_cmd = 0;
                    while(1){
                        commands[i_cmd] = *cmd_list.first;
                        if(cmd_list.rest == NULL){
                            break;
                        }
                        cmd_list = *cmd_list.rest;
                        i_cmd++;
                    }

                    //CREATE PIPELINE MASTER PROCESS
                    pid_t pid_mstr;
                    if((pid_mstr = fork())==0){
                        printf("Mater Pipeline Process, PID = %d, PGID = %d\n", getpid(), getpgrp());

                       //EXECUTE COMMANDS CONCURRENTLY
                        pid_t pid_arr[i_cmd+1];
                        for(int i =0; i <= i_cmd; i++){
                            if((pid_arr[i]=fork())==0){
                                printf("Child of Master, PID = %d. Parent PID = %d PGID = %d\n", getpid(), getppid(), getpgrp());
                                COMMAND cmd = commands[i];
                                WORD_LIST wrd_list = *cmd.words;
                                char* argv_list[100];
                                int i_wrd = 1;
                                while(1){
                                    argv_list[i_wrd-1] = wrd_list.first;
                                    if(wrd_list.rest == NULL){
                                        argv_list[i_wrd] = NULL;
                                        break;
                                    }
                                    wrd_list = *wrd_list.rest;
                                    i_wrd++;
                                }
                                    execvp(cmd.words->first, argv_list);

                            }
                        }

                        //REAPPPPPPPP
                        for(int i =0; i <= i_cmd; i++){
                            pid_t wpid = waitpid(pid_arr[i], &child_status, 0);
                            if(WIFEXITED(child_status))
                                printf("Child of Master, PPID = %d, terminated with exit status %d\n", wpid, WEXITSTATUS(child_status));
                        }

                        exit(child_status);
                    }
                    else{
                        pid_t wpid = waitpid(pid_mstr, &child_status, 0);
                        if(WIFEXITED(child_status))
                            printf("Mater Pipeline Process %d terminated with exit status %d\n", wpid, WEXITSTATUS(child_status));
                    }


                    //GET COMMAND LIST OF NEXT PIPELINE
                    if(pipe_to_execute.rest == NULL)
                        break;
                    pipe_to_execute = *pipe_to_execute.rest;
                    cmd_list = *pipe_to_execute.first->commands;

                }
                exit(child_status);
            }
            else{
                sigprocmask(SIG_SETMASK, &prev, NULL);
                pid_t wpid = waitpid(pid_runner, &child_status, 0);
                jobs_table[i].exit_status = WEXITSTATUS(child_status);
                jobs_table[i].pgid = wpid;
                if(WIFEXITED(child_status))
                    printf("Runner Process %d terminated with exit status %d\n", wpid, WEXITSTATUS(child_status));
                sf_job_end(i, wpid, WEXITSTATUS(child_status));
            }
            jobs_table[i].status = 5;
            sf_job_status_change(i, 2, 5);
        }
    }

    return prev_flag;
}

int jobs_get_enabled() {
    if(enabled_flag == 1)
        return 1;
    else
        return 0;
}

int job_create(char *command) {             // ** NEED TO FREE COPY **
    char* copy = malloc(strlen(command)+1);
    strcpy(copy, command);
    if(parse_task(&copy) == NULL || number_of_jobs == MAX_JOBS){
        return -1;
    }

    //FIND UNOCCUPIED JOB SLOT
    int jobslot;
    for(jobslot = 0; jobslot<MAX_JOBS; jobslot++){
        if(jobs_table[jobslot].occupied == 0)
            break;
    }

    //ADD JOB TO TABLE
    char* taskspec = command + 7;
    taskspec[strlen(taskspec) - 1] = 0;
    jobs_table[jobslot].taskspec = taskspec;
    jobs_table[jobslot].occupied = 1;
    TASK task = *parse_task(&taskspec);
    jobs_table[jobslot].task = task;
    sf_job_create(jobslot);
    jobs_table[jobslot].status = 1;
    sf_job_status_change(jobslot, 0, 1);
    number_of_jobs++;

    // IF ENABLE FLAG SET AND RUNNER PRCOCESS AVAILABLE THEN START THE JOB
    if(jobs_get_enabled() == 1 && number_of_runner_processes < 4){

        jobs_table[jobslot].status = 2;
        sf_job_status_change(jobslot, 1, 2);
        // sf_job_start(jobslot, );

        //CREATE RUNNER PROCESS
        number_of_runner_processes++;
        int child_status;
        pid_t pid_runner;

        //BLOCK SIGCHLD SIGNAL
        sigset_t mask, prev;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, &prev);

        if((pid_runner = fork()) == 0){
            setpgid(0,0);
            sf_job_start(jobslot, getpgrp());
            printf("Runner Process, PID = %d, PGID = %d\n", getpid(), getpgrp());

            //GET COMMAND LIST THAT CHANGES AFTER EVERY PIPELINE
            TASK task = jobs_table[jobslot].task;
            PIPELINE_LIST pipe_to_execute = *task.pipelines;
            COMMAND_LIST cmd_list = *pipe_to_execute.first->commands;

            //EXECUTE ALL PIPELINES SEQEUNTIALLY
            while(1){

                //CREATE COMMANDS ARRAY TO EXECUTE CONCURRENTLY
                COMMAND commands[100];
                int i_cmd = 0;
                while(1){
                    commands[i_cmd] = *cmd_list.first;
                    if(cmd_list.rest == NULL){
                        break;
                    }
                    cmd_list = *cmd_list.rest;
                    i_cmd++;
                }

                //CREATE PIPELINE MASTER PROCESS
                pid_t pid_mstr;
                int child_status;
                if((pid_mstr = fork())==0){
                    printf("Mater Pipeline Process, PID = %d, PGID = %d\n", getpid(), getpgrp());

                    //PIPING
                    int p[2];
                    if(pipe(p) < 0)
                        exit(1);

                   //EXECUTE COMMANDS CONCURRENTLY
                    pid_t pid_arr[i_cmd+1];
                    int child_status;
                    for(int i =0; i <= i_cmd; i++){
                        if((pid_arr[i]=fork())==0){
                            printf("Child of Master, PID = %d. Parent PID = %d PGID = %d\n", getpid(), getppid(), getpgrp());
                            COMMAND cmd = commands[i];
                            WORD_LIST wrd_list = *cmd.words;
                            char* argv_list[100];
                            int i_wrd = 1;
                            while(1){
                                argv_list[i_wrd-1] = wrd_list.first;
                                if(wrd_list.rest == NULL){
                                    argv_list[i_wrd] = NULL;
                                    break;
                                }
                                wrd_list = *wrd_list.rest;
                                i_wrd++;
                            }

                            if(i != 0){
                                while(read(p[0], argv_list[i_wrd], 1) != -1){}
                                argv_list[i_wrd + 1] = NULL;
                            }

                            if(i != i_cmd)
                                dup2(p[1], 1);
                            execvp(cmd.words->first, argv_list);

                        }
                    }

                    //REAPPPPPPPP
                    for(int i =0; i <= i_cmd; i++){
                        pid_t wpid = waitpid(pid_arr[i], &child_status, 0);
                        if(WIFEXITED(child_status))
                            printf("Child of Master, PPID = %d, terminated with exit status %d\n", wpid, WEXITSTATUS(child_status));
                    }

                    exit(child_status);
                }
                else{
                    pid_t wpid = waitpid(pid_mstr, &child_status, 0);
                    if(WIFEXITED(child_status))
                        printf("Mater Pipeline Process %d terminated with exit status %d\n", wpid, WEXITSTATUS(child_status));
                }


                //GET COMMAND LIST OF NEXT PIPELINE
                if(pipe_to_execute.rest == NULL)
                    break;
                pipe_to_execute = *pipe_to_execute.rest;
                cmd_list = *pipe_to_execute.first->commands;

            }
            exit(child_status);
        }
        else{
            sigprocmask(SIG_SETMASK, &prev, NULL);
            pid_t wpid = waitpid(pid_runner, &child_status, 0);
            jobs_table[jobslot].exit_status = WEXITSTATUS(child_status);
            jobs_table[jobslot].pgid = wpid;
            if(WIFEXITED(child_status))
                printf("Runner Process %d terminated with exit status %d\n", wpid, WEXITSTATUS(child_status));
            sf_job_end(jobslot, wpid, WEXITSTATUS(child_status));
        }
        jobs_table[jobslot].status = 5;
        sf_job_status_change(jobslot, 2, 5);
    }

    return jobslot;
}

int job_expunge(int jobid) {
    if(jobs_table[jobid].status == 5 || jobs_table[jobid].status == 6){
        struct job job = {};
        jobs_table[jobid] = job;
        number_of_jobs--;
        sf_job_expunge(jobid);
        return 0;
    }

    return -1;
}

int job_cancel(int jobid) {
    if(( (jobs_table[jobid].occupied == 1)) && (jobs_table[jobid].status != 5) && (jobs_table[jobid].status != 6) ){
        if(jobs_table[jobid].status == 1){
            JOB_STATUS stat = jobs_table[jobid].status;
            jobs_table[jobid].status = 6;
            sf_job_status_change(jobid, stat, 6);
            return 0;
        }
        else if((jobs_table[jobid].status == 3) || (jobs_table[jobid].status == 2)){
            JOB_STATUS stat = jobs_table[jobid].status;
            jobs_table[jobid].status = 4;
            sf_job_status_change(jobid, stat, 4);
            kill(jobs_table[jobid].pgid, SIGKILL);
            jobs_table[jobid].status = 6;
            sf_job_status_change(jobid, stat, 6);
            return 0;
        }
        else
            printf("Error: Job cannot be canceled.\n");
    }
    else
        printf("ERROR: Invalid JobID\n");
    return -1;
}

int job_pause(int jobid) {
    if(jobid < MAX_JOBS && jobid >= 0 && jobs_table[jobid].occupied == 1){
        if((jobs_table[jobid].status == 2) && (kill(jobs_table[jobid].pgid, SIGSTOP) == 0)){
            jobs_table[jobid].status = 3;
            sf_job_status_change(jobid, 2, 3);
            return 0;
        }
        else
            printf("Error: Job not running. Cannot pause.\n");
    }
    else
        printf("Error: Invalid JobID\n");
    return -1;
}

int job_resume(int jobid) {
    if(jobid < MAX_JOBS && jobid >= 0 && jobs_table[jobid].occupied == 1){
        if((jobs_table[jobid].status == 3) && (kill(jobs_table[jobid].pgid, SIGCONT) == 0)){
            jobs_table[jobid].status = 2;
            sf_job_status_change(jobid, 3, 2);
            return 0;
        }
        else
            printf("Error: Job not pasued. Cannot resume.\n");
    }
    else
        printf("Error: Invalid JobID\n");
    return -1;
}

int job_get_pgid(int jobid) {
    if(jobs_table[jobid].status == 2 || jobs_table[jobid].status == 3 || jobs_table[jobid].status == 4)
        return jobs_table[jobid].pgid;
    else
        return -1;
}

JOB_STATUS job_get_status(int jobid) {
    if((jobid < MAX_JOBS) && (jobid >= 0) && (jobs_table[jobid].occupied == 1))
        return jobs_table[jobid].status;
    else
        return -1;
}

int job_get_result(int jobid) {
    if(jobid < MAX_JOBS && jobid >= 0 && jobs_table[jobid].occupied == 1)
        return jobs_table[jobid].exit_status;
    else
        return -1;
}

int job_was_canceled(int jobid) {
    if(jobs_table[jobid].status == 4)
        return 1;
    else
        return 0;
}

char *job_get_taskspec(int jobid) {
    if(jobid < MAX_JOBS && jobid >= 0 && jobs_table[jobid].occupied == 1)
        return jobs_table[jobid].taskspec;
    else
        return NULL;
}
