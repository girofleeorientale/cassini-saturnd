#include "client-request.h"
#include "portable_endian.h"
#include "requests-handler.h"
#include "server-reply.h"
#include "tasks.h"
#include "timing-text-io.h"
#include "timing.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

char * pipes_directory = NULL;
char * tasks_directory = NULL;
int taskctr_fd = -1;

void signal_handler(int signum) {
    free(res_pipe_path);
    free(req_pipe_path);
    free(pipes_directory);
    free(tasks_directory);
    if (taskctr_fd > 0)
        close(taskctr_fd);
    exit(0);
}

int main(int argc, char * argv[]) {
    uint64_t task_ctr = 0;
    pid_t task_launcher_pid;
    struct Pipes pipes;

    //setup signal handler for saturnd and the task launcher
    struct sigaction action;
    memset( & action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigaction(SIGTERM, & action, NULL);

    int opt;
    while ((opt = getopt(argc, argv, "p:t:")) != -1) {
        switch (opt) {
        case 'p':
            pipes_directory = strdup(optarg);
            if (pipes_directory == NULL)
                goto error;
            break;
        case 't':
            tasks_directory = strdup(optarg);
            if (tasks_directory == NULL)
                goto error;
            break;
        }
    }

    if (pipes_directory == NULL) {
        if (mkdefault_subdirs(0775, 0) != 0)
            goto error;
    } else {
        if (construct_pipe_paths(pipes_directory) != 0)
            goto error;
    }

    if (mkpipes(0664) != 0)
        goto error;

    if (tasks_directory == NULL) {
        if (mkdefault_subdirs(0775, 1) != 0)
            goto error;
        char * username = getlogin();
        tasks_directory = malloc(strlen(username) + 20);
        if (build_default_task_path(tasks_directory) != 0)
            goto error;
    }

    //load task counter from task_count file, create it if it doesn't exist
    chdir(tasks_directory);
    int taskctr_fd = open("task_count", O_RDONLY);
    if (taskctr_fd == -1) {
        if (errno == ENOENT) {
            taskctr_fd =
                open("task_count", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (taskctr_fd == -1) {
                perror("[-] Error creating task_count file");
                goto error;
            }
            if (write(taskctr_fd, & task_ctr, sizeof(task_ctr)) < 0) {
                perror("[-] Error writing task counter to disk");
                goto error;
            }
        }
    } else {
        if (read(taskctr_fd, & task_ctr, sizeof(task_ctr)) < 0)
            goto error;
    }
    close(taskctr_fd);
    taskctr_fd = open("task_count", O_WRONLY);
    if (taskctr_fd == -1) {
        perror("[-] Error opening task_count in write mode");
        goto error;
    }

    // fork saturnd to make the task launcher
    task_launcher_pid = fork();
    if (task_launcher_pid == 0) {
        while (1) {
            //wait until the next minute starts
            time_t timestamp = time(NULL);
            struct tm *bd_time = localtime(&timestamp);
            int time_till_next_minute = 59-bd_time->tm_sec+2;
            sleep(time_till_next_minute);


            uint32_t taskcount;
            struct Task ** tasklist = load_all_tasks(tasks_directory, & taskcount);
            if (tasklist == NULL) {
                perror("[-] Error loading all tasks");
                break;
            }
            printf("[+] (Re)loaded %d task(s)\n", taskcount);
            
            //for each task
            for (int i = 0; i < taskcount; i++) {
                struct Task * task = tasklist[i];
                //if timing of the task is now
                if (is_timing_now(task -> timing)) {
                    pid_t pid = fork();
                    if (pid == 0) {
                        //redirect stdout and stderr to tasks output files
						char taskstdoutpath[255];
						char taskstderrpath[255];
						sprintf(taskstdoutpath, "%s/%ld/stdout", tasks_directory, task->task_id);
						sprintf(taskstderrpath, "%s/%ld/stderr", tasks_directory, task->task_id);
						int stdoutfile = open(taskstdoutpath, O_WRONLY | O_TRUNC);
						int stderrfile = open(taskstderrpath, O_WRONLY | O_TRUNC);
						if(stdoutfile < 0 || stderrfile < 0) {
							perror("[-] Error opening task stdout or stderr file");
							goto error;
						}
						int d1 = dup2(stdoutfile, STDOUT_FILENO);
						int d2 = dup2(stderrfile, STDERR_FILENO);
						if(d1 < 0 || d2 < 0) {
							perror("[-] stdout/stderr duplication failed");
							goto error;
						}
                        close(stdoutfile);
                        close(stderrfile);
                        //execute the command
                        execvp(task -> argv[0], task -> argv);
                    } else {
                        //wait until task is done, get exitcode, and write exec time
                        // & exitcode it its struct
                        int status;
                        waitpid(pid, & status, 0);
                        printf("[+] Task %ld exited with status %d\n", task -> task_id,
                            WEXITSTATUS(status));
                        task -> nbruns++;
                        task -> times = realloc(task -> times, task -> nbruns * (sizeof(uint64_t)));
                        task -> times[task -> nbruns - 1] = time(NULL);
                        task -> exitcodes = realloc(task -> exitcodes, task -> nbruns * (sizeof(uint16_t)));
                        task -> exitcodes[task -> nbruns - 1] = WEXITSTATUS(status);
                        
                        //update task on disk
                        if (write_task_to_disk(task, tasks_directory) == -1) {
                            perror("[-] Error writing task to disk");
                            goto error;
                        }

                    }
                }
                free_task(task);
            }
            free(tasklist);
        }
        goto error;
    }

    //main saturnd loop to handle requests
    while (1) {
        uint16_t request;
        //read request
        if (open_pipe( & pipes, REQUEST_PIPE_ID, PIPE_OPEN_MODE_SATURND) == -1)
            goto error;
        read(pipes.req_pipe, & request, sizeof(request));
        close(pipes.req_pipe);
        request = be16toh(request);
        if (open_pipe( & pipes, REQUEST_PIPE_ID, PIPE_OPEN_MODE_SATURND) == -1)
            goto error;

        //handle request with appropriate function
        switch (request) {
        case CLIENT_REQUEST_LIST_TASKS:
            puts("[*] Got list task request");
            if (d_list_task(pipes, tasks_directory))
                goto error;
            break;
        case CLIENT_REQUEST_CREATE_TASK:
            if (d_create_task(pipes, & task_ctr, tasks_directory))
                goto error;
            break;
        case CLIENT_REQUEST_REMOVE_TASK:
            puts("[*] Got remove task request");
            if (d_remove_task(pipes, tasks_directory))
                goto error;
            break;
        case CLIENT_REQUEST_GET_TIMES_AND_EXITCODES:
            puts("[*] Got times and exitcodes request");
            if (d_get_times_and_exit_codes(pipes, tasks_directory))
                goto error;
            break;
        case CLIENT_REQUEST_TERMINATE:
            ;
            puts("[*] Got terminate request");
            uint16_t response = htobe16(SERVER_REPLY_OK);

            if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND) == -1)
                goto error;

            if (write(pipes.res_pipe, & response, sizeof(response)) == -1) {
                perror("[-] Error sending REPLY_OK response to client");
                goto error;
            }

            if (close(pipes.req_pipe) == -1) {
                perror("[-] Error closing request pipe");
                goto error;
            }

            if (close(pipes.res_pipe) == -1) {
                perror("[-] Error closing response pipe");
                goto error;
            }
            goto success;
            break;
        case CLIENT_REQUEST_GET_STDOUT:
            puts("[*] Got get stdout request");
            if (d_get_stdout(pipes, tasks_directory))
                goto error;
            break;
        case CLIENT_REQUEST_GET_STDERR:
            puts("[*] Got get stderr request");
            if (d_get_stderr(pipes, tasks_directory))
                goto error;
            break;
        }
        lseek(taskctr_fd, 0, SEEK_SET);
        if (write(taskctr_fd, & task_ctr, sizeof(task_ctr)) < 0) {
            perror("[-] Error writing task counter to disk");
            goto error;
        }
    }

    success:
    puts("[+] Quitting");
    kill(task_launcher_pid, SIGTERM);
    free(pipes_directory);
    free(tasks_directory);
    free(res_pipe_path);
    free(req_pipe_path);
    if (taskctr_fd > 0)
        close(taskctr_fd);
    return EXIT_SUCCESS;

    error:
    puts("[-] Quitting with error exitcode");
    kill(task_launcher_pid, SIGTERM);
    free(req_pipe_path);
    free(tasks_directory);
    free(pipes_directory);
    free(res_pipe_path);
    if (taskctr_fd > 0)
        close(taskctr_fd);
    return EXIT_FAILURE;
}