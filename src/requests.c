#include "client-request.h"
#include "server-reply.h"
#include "timing.h"
#include "portable_endian.h"
#include "timing-text-io.h"
#include "cassini.h"
#include <endian.h>
#include <stdint.h>

// create a new task and print its id
uint64_t create_task(struct Pipes pipes,
    const struct timing * t,
        const struct CommandLine * cmdline) {
    size_t message_size = sizeof(uint64_t) +
        sizeof(uint32_t) * 2 +
        sizeof(uint16_t) +
        sizeof(uint8_t);

    for (int i = 0; i < cmdline -> argc; i++) {
        message_size += strlen(cmdline -> argv[i]) + sizeof(uint32_t);
    }

    char * message = malloc(message_size);
    uint16_t response;
    uint64_t task_id = -1;

    size_t size = 0;

    uint16_t opcode = htobe16(CLIENT_REQUEST_CREATE_TASK);

    // take necessary fields from timing structure
    uint64_t minutes = htobe64(t -> minutes);
    uint32_t hours = htobe32(t -> hours);
    uint8_t daysofweek = t -> daysofweek;

    uint32_t argc = htobe32(cmdline -> argc);

    // create message with necessary data
    memcpy(message, & opcode, sizeof(uint16_t));
    size += sizeof(opcode);

    memcpy(message + size, & minutes, sizeof(minutes));
    size += sizeof(minutes);

    memcpy(message + size, & hours, sizeof(hours));
    size += sizeof(hours);

    memcpy(message + size, & daysofweek, sizeof(daysofweek));
    size += sizeof(daysofweek);

    memcpy(message + size, & argc, sizeof(argc));
    size += sizeof(argc);

    // copy to message commandline arguments
    for (int i = 0; i < cmdline -> argc; i++) {
        uint32_t arglen = (uint32_t) strlen(cmdline -> argv[i]);
        uint32_t arglenBE = htobe32(arglen);

        memcpy(message + size, & arglenBE, sizeof(arglenBE));
        size += sizeof(arglen);

        memcpy(message + size, (cmdline -> argv[i]), arglen);
        size += arglen;
    }

    // write message to request pipe
    if (write(pipes.req_pipe, message, message_size) == -1) {
        perror("[-] Error sending CREATE_TASK request to daemon");
        free(message);
        return (-1);
    }

    free(message);

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return -1;

    // read from response pipe
    if (read(pipes.res_pipe, & response, sizeof(response)) == -1) {
        perror("[-] Error receiving response from daemon");
        return (-1);
    }

    // take taskid from daemon
    if (read(pipes.res_pipe, & task_id, sizeof(task_id)) == -1) {
        perror("[-] Error receiving task_id from daemon");
        return (-1);
    }

    response = be16toh(response);
    task_id = be64toh(task_id);

    // error case
    if (response == SERVER_REPLY_ERROR) {
        perror("[-] Error creating task");
        return (-1);
    }

    return (task_id);
}

// remove a task from taskslist
uint16_t remove_task(struct Pipes pipes, uint64_t task_id) {
    // calculate message size and allocate memory for it
    size_t message_size = sizeof(uint64_t) + sizeof(uint16_t);
    char * message = malloc(message_size);
    uint16_t response;
    uint16_t recv_errno = 0;
    uint16_t opcode = htobe16(CLIENT_REQUEST_REMOVE_TASK);
    task_id = htobe64(task_id);

    size_t size = 0;

    // add opcode and task id to message 
    memcpy(message, & opcode, sizeof(opcode));
    size += sizeof(opcode);

    memcpy(message + size, & task_id, sizeof(task_id));
    size += sizeof(task_id);

    // send message to daemon
    if (write(pipes.req_pipe, message, message_size) == -1) {
        perror("[-] Error sending REMOVE_TASK request to daemon");
        free(message);
        return (-1);
    }

    free(message);

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return -1;

    // take response from daemon
    if (read(pipes.res_pipe, & response, sizeof(response)) == -1) {
        perror("[-] Error receiving response from daemon");
        return (-1);
    }

    response = be16toh(response);

    // error case
    if (response == SERVER_REPLY_ERROR) {
        if (read(pipes.res_pipe, & recv_errno, sizeof(recv_errno)) == -1) {
            perror("[-] Error receiving errno from daemon");
            return (-1);
        }

        recv_errno = be16toh(recv_errno);
        puts("[-] Error : the specified task doesn't exist");
        return (recv_errno);
    }

    return (0);
}

// get info (time + exitcodes) for previous runs of a task
uint16_t get_times_and_exit_codes(struct Pipes pipes, uint64_t task_id) {
    // calculate message size and allocate memory for it
    size_t message_size = sizeof(uint64_t) + sizeof(uint16_t);
    char * message = malloc(message_size);
    uint16_t response;
    uint16_t errormsg;
    uint16_t opcode = htobe16(CLIENT_REQUEST_GET_TIMES_AND_EXITCODES);
    task_id = htobe64(task_id);

    size_t size = 0;

    // add opcode and task id to message 
    memcpy(message, & opcode, sizeof(opcode));
    size += sizeof(opcode);

    memcpy(message + size, & task_id, sizeof(task_id));
    size += sizeof(task_id);

    // write message to request pipe
    if (write(pipes.req_pipe, message, message_size) == -1) {
        perror("[-] Error sending TIMES_AND_EXIT_CODES request to daemon");
        free(message);
        return (-1);
    }

    free(message);

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return -1;

    // take response from daemon
    if (read(pipes.res_pipe, & response, sizeof(response)) == -1) {
        perror("[-] Error receiving response from daemon");
        return (-1);
    }

    response = be16toh(response);

    // if a response "ok" is received from server
    if (response == SERVER_REPLY_OK) {
        uint32_t nbruns;

        // reading the number of previous task executions
        if (read(pipes.res_pipe, & nbruns, sizeof(nbruns)) == -1) {
            perror("[-] Error receiving nbruns from daemon");
            return (-1);
        }

        nbruns = be32toh(nbruns);

        uint64_t runtime;
        uint16_t exitcode;

        // receive runtime and exitcode from response pipe nbruns times
        for (int i = 0; i < nbruns; i++) {
            if (read(pipes.res_pipe, & runtime, sizeof(runtime)) == -1) {
                perror("[-] Error receiving runtime of task from daemon");
                return (-1);
            }

            if (read(pipes.res_pipe, & exitcode, sizeof(exitcode)) == -1) {
                perror("[-] Error receiving exitcode of task from daemon");
                return (-1);
            }

            runtime = be64toh(runtime);
            time_t runtime_time_t = (time_t) runtime;
            struct tm * runtime_struct = localtime( & runtime_time_t);

            //Need to print all as double digits, add 1900 to year and add 1 to month since range is (0-11)
            printf("%02d-%02d-%02d %02d:%02d:%02d ", (runtime_struct -> tm_year) + 1900,
                runtime_struct -> tm_mon + 1, runtime_struct -> tm_mday, runtime_struct -> tm_hour,
                runtime_struct -> tm_min, runtime_struct -> tm_sec);
            printf("%d\n", exitcode);
        }
        return (0);
    } else {
        if (read(pipes.res_pipe, &errormsg, sizeof(errormsg)) == -1) {
            perror("[-] Error receiving response from daemon");
            return (-1);
        }
        errormsg = be16toh(errormsg);
        switch (errormsg) {
            case SERVER_REPLY_ERROR_NOT_FOUND:
                puts("[-] Error : specified task doesn't exist");
                break;
            case SERVER_REPLY_ERROR_NEVER_RUN:
                puts("[-] Error : specified task was never ran");
                break;   
        }
    }
    return (-1);
}

// list all tasks
void list_tasks(struct Pipes pipes) {
    uint16_t request = htobe16(CLIENT_REQUEST_LIST_TASKS);

    // writing request to request pipe
    if (write(pipes.req_pipe, & request, 2) != 2) {
        perror("[-] Error writing to pipe");
        exit(-1);
    }

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return;

    // reading response from response pipe
    uint16_t response;
    read(pipes.res_pipe, & response, 2);
    response = be16toh(response);
    uint32_t nb_tasks;

    // consider cases of response "OK" or "ERROR"
    switch (response) {
    case SERVER_REPLY_OK:
        // read task number from response pipe
        if (read(pipes.res_pipe, & nb_tasks, sizeof(nb_tasks)) == -1) {
            perror("[-] Error reading tasks number");
            exit(-1);
        }
        nb_tasks = be32toh(nb_tasks);
        // allocate memory for timing structure
        char * timing_str = malloc(TIMING_TEXT_MIN_BUFFERSIZE);

        // for every task, read from response pipe its id, timing (minutes, hours, day of week) and arguments
        for (int i = 0; i < nb_tasks; i++) {
            uint64_t task_id;
            if (read(pipes.res_pipe, & task_id, sizeof(task_id)) == -1) {
                perror("[-] Error reading task id");
                exit(-1);
            }
            task_id = htobe64(task_id);
            printf("%ld: ", task_id);

            struct timing time;
            uint64_t minutes;
            if (read(pipes.res_pipe, & minutes, sizeof(uint64_t)) == -1) {
                perror("[-] Error reading minutes");
                exit(-1);
            }
            uint32_t hours;
            if (read(pipes.res_pipe, & hours, sizeof(uint32_t)) == -1) {
                perror("[-] Error reading hours");
                exit(-1);
            }
            uint8_t days;
            if (read(pipes.res_pipe, & days, sizeof(uint8_t)) == -1) {
                perror("[-] Error reading days");
                exit(-1);
            }

            minutes = htobe64(minutes);
            hours = htobe32(hours);

            time.minutes = minutes;
            time.hours = hours;
            time.daysofweek = days;

            // create string from timing structure and print it
            timing_string_from_timing(timing_str, & time);
            printf("%s ", timing_str);

            uint32_t arg_c;
            if (read(pipes.res_pipe, & arg_c, sizeof(uint32_t)) == -1) {
                perror("[-] Error reading argc");
                exit(-1);
            }
            arg_c = be32toh(arg_c);

            // read argc and command name and print it
            for (int i = 0; i < arg_c; i++) {
                uint32_t word_size;
                if (read(pipes.res_pipe, & word_size, sizeof(uint32_t)) == -1) {
                    perror("[-] Error reading argc");
                    exit(-1);
                }
                word_size = be32toh(word_size);
                char * arg_word = malloc(sizeof(char) * (word_size + 1));
                arg_word[word_size] = '\0';
                if (read(pipes.res_pipe, arg_word, sizeof(char) * (word_size)) == -1) {
                    perror("[-] Error reading argv[i]");
                    exit(-1);
                }

                printf("%s ", arg_word);
                free(arg_word);
            }
            printf("\n");
        }
        free(timing_str);
        exit(0);
        break;
    case SERVER_REPLY_ERROR:
        perror("[-] Error reading response");
        exit(-1);
    }
}

// terminate the daemon
void terminate(struct Pipes pipes) {
    uint16_t request = htobe16(CLIENT_REQUEST_TERMINATE);
    uint16_t response;

    // write terminate request to request pipe
    if (write(pipes.req_pipe, & request, sizeof(request)) == -1) {
        perror("[-] Error sending TERMINATE request to daemon");
        exit(-1);
    }

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return;

    // reading response from response pipe
    if (read(pipes.res_pipe, & response, sizeof(response)) == -1) {
        perror("[-] Error receiving response from daemon");
        exit(-1);
    }
}

// get the standard output of the last run of a task
uint16_t get_stdout(struct Pipes pipes, uint64_t task_id) {
    uint16_t request = htobe16(CLIENT_REQUEST_GET_STDOUT);
    uint16_t errorcode;
    uint16_t response;
    task_id = htobe64(task_id);

    // writing request to the request pipe
    if (write(pipes.req_pipe, & request, sizeof(request)) == -1) {
        perror("[-] Error sending STDOUT request to daemon");
        return -1;
    }

    // writing task id to the request pipe
    if (write(pipes.req_pipe, & task_id, sizeof(task_id)) == -1) {
        perror("[-] Error sending task_id for STDOUT request to daemon");
        return -1;
    }

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return -1;

    // read the response from daemon, return error code in case of error
    if (read(pipes.res_pipe, & response, sizeof(response)) == -1) {
        perror("[-] Error receiving response from daemon");
        return SERVER_REPLY_ERROR;
    }
    response = be16toh(response);
    // considering different response cases: "OK", "NOT FOUND", "NEVER RUN"
    switch (response) {
    case SERVER_REPLY_OK:
        ;
        // in case of "OK" answer, read the size of the stdout, allocate memory for it,
        // read stdout received from daemon in a buffer, print buffer then free it
        uint32_t stdout_size;
        if (read(pipes.res_pipe, & stdout_size, sizeof(stdout_size)) == -1) {
            perror("[-] Error receiving stdout_size from daemon");
            return SERVER_REPLY_ERROR;
        }
        stdout_size = be32toh(stdout_size);
        char * stdout_buffer = malloc((stdout_size + 1) * sizeof(char));

        if (stdout_buffer == NULL) {
            perror("[-] Malloc error in get_stdout");
            return EXIT_FAILURE;
        }
        if (read(pipes.res_pipe, stdout_buffer, stdout_size) == -1) {
            perror("[-] Error receiving stderr from daemon");
            free(stdout_buffer);
            return SERVER_REPLY_ERROR;
        }
        stdout_buffer[stdout_size] = '\0';
        printf("%s", stdout_buffer);
        free(stdout_buffer);
        break;
    // if the task was not found or never ran, return exit failure
    case SERVER_REPLY_ERROR:
        if (read(pipes.res_pipe, & errorcode, sizeof(response)) == -1) {
            perror("[-] Error receiving response from daemon");
            return SERVER_REPLY_ERROR;
        }
        errorcode = be16toh(errorcode);
        switch (errorcode) {
        case SERVER_REPLY_ERROR_NOT_FOUND:
            puts("[-] The specified task couldn't be found");
            return EXIT_FAILURE;
            break;
        case SERVER_REPLY_ERROR_NEVER_RUN:
            puts("[-] The specified task was never ran");
            return EXIT_FAILURE;
            break;
        }    
    default:
        return EXIT_FAILURE;
    }
    return SERVER_REPLY_OK;
}

// get the standard error 
uint16_t get_stderr(struct Pipes pipes, uint64_t task_id) {
    uint16_t request = htobe16(CLIENT_REQUEST_GET_STDERR);
    uint16_t response;
    uint16_t errcode;
    task_id = htobe64(task_id);

    // writing request to the request pipe
    if (write(pipes.req_pipe, & request, sizeof(request)) == -1) {
        perror("[-] Error sending STDERR request to daemon");
        return -1;
    }
    // writing task id to the request pipe
    if (write(pipes.req_pipe, & task_id, sizeof(task_id)) == -1) {
        perror("[-] Error sending task_id for STDERR request to daemon");
        return -1;
    }

    // response pipe opening
    if (open_pipe( & pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_CASSINI) == -1)
        return -1;

    // read the response from daemon, return error code in case of error
    if (read(pipes.res_pipe, & response, sizeof(response)) == -1) {
        perror("[-] Error receiving response from daemon");
        return SERVER_REPLY_ERROR;
    }
    response = be16toh(response);
    switch (response) {
        // considering response cases: "OK" and "ERROR"
    case SERVER_REPLY_OK:
        //read string size
        ;
        uint32_t stderr_size;

        // in case of "OK" answer, read the size of the stderr, allocate memory for it,
        // read stderr received from daemon in a buffer, print buffer then free it
        if (read(pipes.res_pipe, & stderr_size, sizeof(stderr_size)) == -1) {
            perror("[-] Error receiving stderr_size from daemon");
            return SERVER_REPLY_ERROR;
        }
        stderr_size = be32toh(stderr_size);
        char * stderr_buffer = malloc(stderr_size + 1);
        if (stderr_buffer == NULL) {
            perror("[-] Malloc error in get_stderr");
            return SERVER_REPLY_ERROR;
        }
        if (read(pipes.res_pipe, stderr_buffer, stderr_size) == -1) {
            perror("[-] Error receiving stderr from daemon");
            return SERVER_REPLY_ERROR;
        }
        memset(stderr_buffer + stderr_size, 0, 1);
        printf("%s", stderr_buffer);
        free(stderr_buffer);
        break;
        // in case of ERROR answer, receive error code from daemon and then
        // return error code ("NOT FOUND" or "NEVER RUN")
    case SERVER_REPLY_ERROR:
        if (read(pipes.res_pipe, & errcode, sizeof(errcode)) == -1) {
            perror("[-] Error receiving error code from daemon");
            return SERVER_REPLY_ERROR;
        }
        errcode = htobe16(errcode);
        switch (errcode) {
        case SERVER_REPLY_ERROR_NOT_FOUND:
            puts("[-] The specified task couldn't be found");
            return SERVER_REPLY_ERROR_NOT_FOUND;
        case SERVER_REPLY_ERROR_NEVER_RUN:
            puts("[-] The specified task was never ran");
            return SERVER_REPLY_ERROR_NEVER_RUN;
        }
        break;
    }
    return SERVER_REPLY_OK;
}