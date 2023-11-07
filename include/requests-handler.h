#include "utils.h"
#include "timing-text-io.h"
#include "tasks.h"
#include "server-reply.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

/**
* Function to handle the create task request
*/
int d_create_task(struct Pipes pipes, uint64_t * task_ctr, char * task_dir);

/**
* Function to handle the remove task request
*/
int d_remove_task(struct Pipes pipes, char * tasks_dir);

/**
* Function to handle the list tasks request
*/
int d_list_task(struct Pipes pipes, char * task_dir);

/**
* Function to handle the get stdout request
*/
int d_get_stdout(struct Pipes pipes, char * task_dir);

/**
* Function to handle the get stderr request
*/
int d_get_stderr(struct Pipes pipes, char * task_dir);

/**
* Function to handle the get times and exitcodes request
*/
int d_get_times_and_exit_codes (struct Pipes pipes, char * taskdir);
