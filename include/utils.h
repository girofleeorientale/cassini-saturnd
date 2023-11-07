#ifndef UTILS_H
#define UTILS_H

#include "timing.h"
#include <stdint.h>
#include <sys/stat.h>

#define REQUEST_PIPE_ID 0
#define REPLY_PIPE_ID 1
#define PIPE_OPEN_MODE_CASSINI 0
#define PIPE_OPEN_MODE_SATURND 1

struct CommandLine {
    uint32_t argc;
    char **argv;
};

struct Pipes {
  int req_pipe;
  int res_pipe;
};

extern char *req_pipe_path;
extern char *res_pipe_path;

/**
* Updates Pipes structure with new fd after opening specific pipe
* id : REQUEST_PIPE_ID or REPLY_PIPE_ID
* mode : PIPE_OPEN_MODE_CASSINI or PIPE_OPEN_MODE_SATURND
*/
int open_pipe(struct Pipes *pipes, int id, int mode);

/**
* Builds paths (char *) for both pipes and store them in global
* variables
*/
int construct_pipe_paths(char *pipes_directory);


/**
* Builds the default pipe path using the username
*/
int build_default_pipe_path(char *pipe_path);

/**
* Builds the default task path using the username
*/
int build_default_task_path(char *task_path);

/**
* Creates the named pipes in the locations pointed by the
* global variables req_pipe_path and res_pipe_path
*/
int mkpipes(mode_t perms);

/**
* Builds default subdirectory tree for pipes and tasks
*/
int mkdefault_subdirs(mode_t perms, int build_tasks);

/**
* Returns 1 if the provided timing structures matches the current
* local time, else returns 0
* Bit manipulation method taken from
* https://stackoverflow.com/questions/523724/c-c-check-if-one-bit-is-set-in-i-e-int-variable
*/
int is_timing_now(struct timing t);

#endif
