#include <stdlib.h>
#include <errno.h>

#include "jobber.h"
#include "task.h"
#include "moreFunc.h"
#include <string.h>

/*
 * "Jobber" job spooler.
 */

int number_of_jobs;

int enabled_flag;

int number_of_runner_processes;

struct job *jobs_table;


int main(int argc, char *argv[])
{
    //INITIALIZE JOB SPOOLER
    jobs_init();

    //COMMAND LINE PROMPT
    while(1){
        char* output;
        if((output = sf_readline("jobber> ")) == NULL)
            exit(EXIT_SUCCESS);


        //HELP
        if(strcmp(output, "help") == 0){
            printf("Available commands:\nhelp (0 args) Print this help message\nquit (0 args) Quit the program\nenable (0 args) Allow jobs to start\ndisable (0 args) Prevent jobs from starting\nspool (1 args) Spool a new job\npause (1 args) Pause a running job\nresume (1 args) Resume a paused job\ncancel (1 args) Cancel an unfinished job\nexpunge (1 args) Expunge a finished job\nstatus (1 args) Print the status of a job\njobs (0 args) Print the status of all jobs\n");
        }

        //QUIT
        else if(strcmp(output, "quit") == 0){
            jobs_fini();
        }

        // STATUS
        else if((strncmp(output, "status", 6) == 0) && *(output+7) != '\0'){
            char* id = (output + 7);        // ** ADD CHECKERS FOR INVALID STATUS CHECKING AND FIGURE OUT HOW TO REMOVE EXTRA PRINTS**
            int id2 = *id - 48;
            if(id2 < MAX_JOBS && jobs_table[id2].occupied == 1){
                char* status = job_status_names[job_get_status(id2)];
                TASK task = jobs_table[id2].task;
                printf("job ");printf("%d", id2);printf(" [%s]: ", status);
                unparse_task(&task, stdout);printf("\n");
            }
            else
                printf("Job ID does not exist.\n");
        }

        //JOBS
        else if(strcmp(output, "jobs") == 0){
            int i;
            for(i = 0; i < MAX_JOBS; i++){
                if(jobs_table[i].occupied == 1){
                    char* status = job_status_names[jobs_table[i].status];
                    TASK task = jobs_table[i].task;
                    printf("job %d [%s]: ", i, status);
                    unparse_task(&task, stdout);printf("\n");
                }
            }
        }

        //ENABLE
        else if(strcmp(output, "enable") == 0){
            jobs_set_enabled(-1);
        }

        //DISABLE
        else if(strcmp(output, "disable") == 0){
            jobs_set_enabled(0);
        }

        //SPOOL
        else if(strncmp(output, "spool", 5) == 0 && *(output+6) != '\0'){

            // CREATE JOB
            if(job_create(output) == -1){
                printf("ERROR: Job cannot be created. Invalid \n");
            }
        }

        //PAUSE
        else if(strncmp(output, "pause", 5) == 0 && *(output+6) != '\0'){
            char* id = (output + 6);
            int id2 = *id - 48;
            job_pause(id2);
        }

        //RESUME
        else if(strncmp(output, "resume", 6) == 0 && *(output+7) != '\0'){
            char* id = (output + 7);
            int id2 = *id - 48;
            job_resume(id2);
        }

        //CANCEL
        else if(strncmp(output, "cancel", 6) == 0 && *(output+7) != '\0'){
            char* id = (output + 7);
            int id2 = *id - 48;
            job_cancel(id2);
        }

        //EXPUNGE
        else if(strncmp(output, "expunge", 7) == 0 && *(output+8) != '\0'){
            char* id = (output + 8);
            int id2 = *id - 48;
            job_expunge(id2);
        }

    }

    exit(EXIT_FAILURE);
}

/*
 * Just a reminder: All non-main functions should
 * be in another file not named main.c
 */
