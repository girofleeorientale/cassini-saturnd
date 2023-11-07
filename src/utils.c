#include "timing.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "time.h"


char *req_pipe_path;
char *res_pipe_path;

/**
* Updates Pipes structure with new fd after opening specific pipe
* id : REQUEST_PIPE_ID or REPLY_PIPE_ID
* mode : PIPE_OPEN_MODE_CASSINI or PIPE_OPEN_MODE_SATURND
*/
int open_pipe(struct Pipes *pipes, int id, int mode) {
    if (id == REQUEST_PIPE_ID) {
        pipes->req_pipe = open(req_pipe_path, (mode == PIPE_OPEN_MODE_CASSINI) ? O_WRONLY : O_RDONLY);
        if(pipes->req_pipe == -1) {
            perror("[-] Error opening saturnd-request-pipe");
            return -1;
        }
    }
    if (id == REPLY_PIPE_ID) {
        pipes->res_pipe = open(res_pipe_path, (mode == PIPE_OPEN_MODE_CASSINI) ? O_RDONLY : O_WRONLY);
        if (pipes->res_pipe == -1) {
            perror("[-] Error opening saturnd-reply-pipe");
            return -1;
        }
    }
    return 0;
}

/**
* Builds paths (char *) for both pipes and store them in global
* variables
*/
int construct_pipe_paths(char *pipes_directory) {
    int usernameSize = strlen(getlogin());

    req_pipe_path = malloc(40 + usernameSize + 1);
    res_pipe_path = malloc(38 + usernameSize + 1);

    if (req_pipe_path == NULL || res_pipe_path == NULL) {
        perror("[-] Malloc for pipes path");
        return -1;
    }

    sprintf(req_pipe_path, "%s/saturnd-request-pipe", pipes_directory);

    sprintf(res_pipe_path, "%s/saturnd-reply-pipe", pipes_directory);

    return 0;
}

/**
* Builds default subdirectory tree for pipes and tasks
*/
int mkdefault_subdirs(mode_t perms, int build_tasks) {
    int res = 0;
    char *username = getlogin();
    int user_path_length = 5 + strlen(username);
    char *user_path = malloc(user_path_length + 1);
    char *saturnd_path = malloc(user_path_length + 8 + 1);
    char *pipes_path = malloc(user_path_length + 14 + 1);
    char *tasks_path = malloc(user_path_length + 14 + 1);

    sprintf(user_path, "/tmp/%s", username);
    if (mkdir(user_path, perms) != 0 && errno != EEXIST) {
        perror("[-] Error creating /tmp/<localUser> directory");
        res = -1;
    }

    sprintf(saturnd_path, "%s/saturnd", user_path);
    if (mkdir(saturnd_path, perms) != 0 && errno != EEXIST) {
        perror("[-] Error creating /tmp/<localUser>/saturnd directory");
        res = -1;
    }

    sprintf(pipes_path, "%s/pipes", saturnd_path);
    sprintf(tasks_path, "%s/tasks", saturnd_path);

    if (build_tasks) {
        if (mkdir(tasks_path, perms) != 0 && errno != EEXIST) {
            perror("[-] Error creating /tmp/<localUser>/saturnd/tasks directory");
            res = -1;
        }
    }
    else {
        if (mkdir(pipes_path, perms) != 0 && errno != EEXIST) {
            perror("[-] Error creating /tmp/<localUser>/saturnd/pipes directory");
            res = -1;
        }

        construct_pipe_paths(pipes_path);
    }

    free(tasks_path);
    free(pipes_path);
    free(user_path);
    free(saturnd_path);
    return (res);
}

/**
* Creates the named pipes in the locations pointed by the
* global variables req_pipe_path and res_pipe_path
*/
int mkpipes(mode_t perms) {
    if (mkfifo(req_pipe_path, perms) == -1 && errno != EEXIST) {
        perror("[-] Error creating request pipes");
        return (-1);
    }

    if (mkfifo(res_pipe_path, perms) == -1 && errno != EEXIST) {
        perror("[-] Error creating request pipes");
        return (-1);
    }
    return(0);
}

/**
* Builds the default task path using the username
*/
int build_default_task_path(char *task_path) {
	char *username = getlogin();
    sprintf(task_path, "/tmp/%s/saturnd/tasks", username);
    return 0;
}

/**
* Builds the default pipe path using the username
*/
int build_default_pipe_path(char *pipe_path) {
    char *username = getlogin();
    sprintf(pipe_path, "/tmp/%s/saturnd/pipes", username);
    return 0;
}

/**
* Returns 1 if the provided timing structures matches the current
* local time, else returns 0
* Bit manipulation method taken from
* https://stackoverflow.com/questions/523724/c-c-check-if-one-bit-is-set-in-i-e-int-variable
*/
int is_timing_now(struct timing t) {
    time_t timestamp = time(NULL);
    struct tm *bd_time = localtime(&timestamp);

    return  ((t.minutes) & (1<<(bd_time->tm_min)))
         && ((t.hours) & (1<<(bd_time->tm_hour)))
         && ((t.daysofweek) & (1<<(bd_time->tm_wday)));
}
