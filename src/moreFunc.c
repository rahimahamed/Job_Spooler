#include <stdlib.h>

#include "jobber.h"
#include "task.h"
#include "sf_readline.h"
#include "moreFunc.h"

void sigchld_handler(int sig){
    runner_freed_flag = 1;
    number_of_runner_processes--;
}

int sig_chld_handler_func(){
    if(runner_freed_flag == 1){
        runner_freed_flag = 0;
        jobs_set_enabled(-1);
        return 1;
    }
    else{
        runner_freed_flag = 0;
        return 0;
    }
}


