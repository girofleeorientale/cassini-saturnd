#include "timing.h"
#include "client-request.h"
#include "utils.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

// list all tasks
void list_tasks(struct Pipes pipes);
// terminate the daemon
void terminate(struct Pipes pipes);

// Returns the created task ID
// create a new task and print its id
uint64_t create_task(struct Pipes pipes, const struct timing* t, const struct CommandLine* cmdline);

// Returns SERVER_REPLY_OK or SERVER_REPLY_ERROR
uint16_t remove_task(struct Pipes pipes, uint64_t task_id);
// get informations about task proceeding (times the task was ran and exit codes)
uint16_t get_times_and_exit_codes(struct Pipes pipes, uint64_t task_id);
// get the standard output of the last run of a task
uint16_t get_stdout(struct Pipes pipes, uint64_t task_id);
// get the standard error 
uint16_t get_stderr(struct Pipes pipes, uint64_t task_id);
