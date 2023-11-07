#ifndef TASKS_H
#define TASKS_H

#include "timing.h"
#include "utils.h"
#include <stdint.h>
#include <sys/types.h>

struct Task {
    uint64_t task_id;
    uint32_t argc;
    char **argv;
    struct timing timing;
	uint16_t *exitcodes;
	uint64_t *times;
    uint32_t nbruns;
    char *std_out;
    char *std_err;
};

/**
* Takes a task pointer and writes it to disk in the directory pointed by
* tasks_dir. Returns 0 on success and -1 on error.
*/
int write_task_to_disk(struct Task *task, char *tasks_dir);

/**
* Reads the task with identifier taskid in the directory pointed by tasks_dir.
* A pointer to the task is returned. The task must be freed later.
* Returns NULL on error.
*/
struct Task * read_task_from_disk(char *tasks_dir, uint64_t taskid);

/**
* Loads all tasks from the directory pointed by task_dir. Write the number of
* loaded tasks in taskcount. A task is defined as being a subdirectory with
* a name for a number. This function does not check for file types and assumes
* the task_dir was the one created by saturnd and hasn't been tampered with.
*/
struct Task ** load_all_tasks(char *task_dir, uint32_t *taskcount);

/**
* Frees a task structure and all its attributes
*/
void free_task(struct Task *task);
#endif
