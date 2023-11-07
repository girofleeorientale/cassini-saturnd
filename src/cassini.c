#include "cassini.h"
#include "utils.h"
#include <errno.h>
#include <unistd.h>
#include "portable_endian.h"

struct Pipes pipes;
struct CommandLine cmd;

const char usage_info[] = "\
usage: cassini [OPTIONS] -l -> list all tasks\n\
or: cassini [OPTIONS]    -> same\n\
or: cassini [OPTIONS] -q -> terminate the daemon\n\
or: cassini [OPTIONS] -c [-m MINUTES] [-H HOURS] [-d DAYSOFWEEK] COMMAND_NAME [ARG_1] ... [ARG_N]\n\
-> add a new task and print its TASKID\n\
format & semantics of the \"timing\" fields defined here:\n\
https://pubs.opengroup.org/onlinepubs/9699919799/utilities/crontab.html\n\
default value for each field is \"*\"\n\
or: cassini [OPTIONS] -r TASKID -> remove a task\n\
or: cassini [OPTIONS] -x TASKID -> get info (time + exit code) on all the past runs of a task\n\
or: cassini [OPTIONS] -o TASKID -> get the standard output of the last run of a task\n\
or: cassini [OPTIONS] -e TASKID -> get the standard error\n\
or: cassini -h -> display this message\n\
\n\
options:\n\
-p PIPES_DIR -> look for the pipes in PIPES_DIR (default: /tmp/<USERNAME>/saturnd/pipes)\n\
";

int main(int argc, char * argv[]) {
    errno = 0;

    char * minutes_str = "*";
    char * hours_str = "*";
    char * daysofweek_str = "*";
    char * pipes_directory = NULL;

    uint16_t operation = CLIENT_REQUEST_LIST_TASKS;
    uint64_t taskid;

    int opt;
    char * strtoull_endp;
    while ((opt = getopt(argc, argv, "hlcqm:H:d:p:r:x:o:e:")) != -1) {
        switch (opt) {
            case 'm':
                minutes_str = optarg;
                break;
            case 'H':
                hours_str = optarg;
                break;
            case 'd':
                daysofweek_str = optarg;
                break;
            case 'p':
                pipes_directory = strdup(optarg);
                if (pipes_directory == NULL) goto error;
                break;
            case 'l':
                operation = CLIENT_REQUEST_LIST_TASKS;
                break;
            case 'c':
                operation = CLIENT_REQUEST_CREATE_TASK;
                break;
            case 'q':
                operation = CLIENT_REQUEST_TERMINATE;
                break;
            case 'r':
                operation = CLIENT_REQUEST_REMOVE_TASK;
                taskid = strtoull(optarg, &strtoull_endp, 10);
                if (strtoull_endp == optarg || strtoull_endp[0] != '\0') goto error;
                break;
            case 'x':
                operation = CLIENT_REQUEST_GET_TIMES_AND_EXITCODES;
                taskid = strtoull(optarg, &strtoull_endp, 10);
                if (strtoull_endp == optarg || strtoull_endp[0] != '\0') goto error;
                break;
            case 'o':
                operation = CLIENT_REQUEST_GET_STDOUT;
                taskid = strtoull(optarg, &strtoull_endp, 10);
                if (strtoull_endp == optarg || strtoull_endp[0] != '\0') goto error;
                break;
            case 'e':
                operation = CLIENT_REQUEST_GET_STDERR;
                taskid = strtoull(optarg, &strtoull_endp, 10);
                if (strtoull_endp == optarg || strtoull_endp[0] != '\0') goto error;
                break;
            case 'h':
                printf("%s", usage_info);
                return 0;
            case '?':
                fprintf(stderr, "%s", usage_info);
                goto error;
        }
    }

    if(pipes_directory == NULL) {
        pipes_directory = malloc(255);
        build_default_pipe_path(pipes_directory);
    }

    construct_pipe_paths(pipes_directory);

    if(open_pipe(&pipes, REQUEST_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        goto error;

    switch (operation) {
        case CLIENT_REQUEST_LIST_TASKS:
            list_tasks(pipes);
            break;
        case CLIENT_REQUEST_CREATE_TASK:
            if (argc <= 2) {
                printf("Create task: ./cassini -c [-m MINUTES] [-H HOURS] [-d DAYSOFWEEK] COMMAND_NAME [ARG_1] ... [ARG_N]");
                goto error;
            }

            struct timing *t = malloc(sizeof(struct timing));

            if (timing_from_strings(t, minutes_str, hours_str, daysofweek_str) != 0) {
                perror("Error getting timing from arguments");
                free(t);
                goto error;
            }
            int cmd_argc = argc - optind;

            char **cmd_argv = malloc(cmd_argc * sizeof(char *));

            if (cmd_argv) {
                for (cmd_argc = 0; optind < argc; optind++, cmd_argc++) {
                    cmd_argv[cmd_argc] = argv[optind];
                }
            }

            struct CommandLine *cmds = malloc(sizeof(struct CommandLine) + cmd_argc * sizeof(char *));

            if (cmds) {
                cmds -> argc = cmd_argc;
                cmds -> argv = cmd_argv;
            }

            printf("%ld\n", create_task(pipes, t, cmds));
            free(cmd_argv);
            free(cmds);
            free(t);
            break;
        case CLIENT_REQUEST_GET_TIMES_AND_EXITCODES:
            if (get_times_and_exit_codes(pipes, taskid) != 0)
                goto error;
            break;
        case CLIENT_REQUEST_TERMINATE:
            terminate(pipes);
            break;
        case CLIENT_REQUEST_GET_STDERR:
            if (get_stderr(pipes, taskid) == EXIT_FAILURE)
            	goto error;
            break;
        case CLIENT_REQUEST_GET_STDOUT:
            if (get_stdout(pipes, taskid) == EXIT_FAILURE)
            	goto error;
            break;
        case CLIENT_REQUEST_REMOVE_TASK:
            remove_task(pipes, taskid);
            break;
        default:
            goto error;

    }

    close(pipes.req_pipe);
    close(pipes.res_pipe);
    free(pipes_directory);
    free(req_pipe_path);
    free(res_pipe_path);
    return EXIT_SUCCESS;

    error:
    free(pipes_directory);
    free(req_pipe_path);
    free(res_pipe_path);
    close(pipes.req_pipe);
    close(pipes.res_pipe);

    return EXIT_FAILURE;
}
