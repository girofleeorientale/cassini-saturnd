#include "requests-handler.h"
#include "tasks.h"
#include "timing-text-io.h"
#include "utils.h"
#include "server-reply.h"
#include <endian.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include "portable_endian.h"


/**
* Function to handle the create task request
*/
int d_create_task(struct Pipes pipes, uint64_t * task_ctr, char * task_dir) {
	struct timing * t = malloc(sizeof(struct timing));
	uint64_t minutes;
	uint32_t hours;
	uint8_t daysofweek;

	uint32_t argc;
	uint32_t arglen;

	struct Task * task;

	int task_size = sizeof(uint64_t) +		//taskid
					sizeof(uint32_t) +		//argc
					sizeof(char **) +		//argv
					sizeof(struct timing) +	//timing
					sizeof(uint16_t *) +	//exitcodes arrays
					sizeof(uint64_t *) +	//times array
					sizeof(uint32_t) +		//nbruns
					2 * sizeof(char *)		//stdout & stderr
					+ 8; 					//task struct

	//read the timing
	read(pipes.req_pipe, &minutes, sizeof(uint64_t));
	read(pipes.req_pipe, &hours, sizeof(uint32_t));
	read(pipes.req_pipe, &daysofweek, sizeof(uint8_t));

	minutes = be64toh(minutes);
	hours = be32toh(hours);

	t -> minutes = minutes;
	t -> hours = hours;
	t -> daysofweek = daysofweek;

	//read the argc
	read(pipes.req_pipe, &argc, sizeof(uint32_t));
	argc = be32toh(argc);

	char ** argv = malloc((argc+1) * sizeof(char *));

	//read the argv
	for (int i = 0; i < argc; i++) {
		read(pipes.req_pipe, &arglen, sizeof(uint32_t));
		arglen = be32toh(arglen);
		task_size += arglen + 1;

		argv[i] = malloc(arglen + 1);
		read(pipes.req_pipe, argv[i], arglen);
		argv[i][arglen] = 0;
	}

	*task_ctr += 1;
	printf("%ld\n", *task_ctr);
	task = malloc(task_size);
	task -> task_id = *task_ctr;
	task -> argc = argc;
	task -> argv = argv;
	task -> timing = *t;
	task -> nbruns = 0;
	task -> std_out = NULL;
	task -> std_err = NULL;
	task -> times = NULL;
	task -> exitcodes = NULL;

	//send the OK response to cassini
	uint16_t ok = htobe16(SERVER_REPLY_OK);
	uint64_t task_id = htobe64(*task_ctr);

	if (open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND) == -1) {
		perror("[-] Error opening response pipe");
		return(-1);
	}

	write(pipes.res_pipe, &ok, sizeof(uint16_t));
	write(pipes.res_pipe, &task_id, sizeof(uint64_t));

	close(pipes.res_pipe);
	write_task_to_disk(task, task_dir);


	free_task(task);
	free(t);
	return(0);
}

/**
* Function to handle the list tasks request
*/
int d_list_task(struct Pipes pipes, char * task_dir) {
	//load all tasks
	uint32_t taskcount;
	struct Task **tasklist = load_all_tasks(task_dir, &taskcount);
	if(tasklist == NULL)
		return -1;
	if(open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND))
		return -1;

	//send OK and the task count to cassini
	uint16_t ok = htobe16(SERVER_REPLY_OK);
	if(write(pipes.res_pipe, &ok, sizeof(ok)) < 0)
		return -1;
	uint32_t taskcount_BE = htobe32(taskcount);
	if(write(pipes.res_pipe, &taskcount_BE, sizeof(uint32_t)) < 0)
		return -1;

	//send each task
	for (int i=0; i<taskcount; i++) {
		struct Task *task = tasklist[i];
		uint64_t taskid = htobe64(task->task_id);
		uint64_t minutes = htobe64(task->timing.minutes);
		uint32_t hours = htobe32(task->timing.hours);
		uint8_t daysofweek = task->timing.daysofweek;
		uint32_t argc = htobe32(task->argc);
		if(write(pipes.res_pipe, &taskid, sizeof(taskid)) < 0)
			return -1;
		if(write(pipes.res_pipe, &minutes, sizeof(minutes)) < 0)
			return -1;
		if(write(pipes.res_pipe, &hours, sizeof(hours)) < 0)
			return -1;
		if(write(pipes.res_pipe, &daysofweek, sizeof(daysofweek)) < 0)
			return -1;
		if(write(pipes.res_pipe, &argc, sizeof(argc)) < 0)
			return -1;
		for (int j=0; j < task->argc; j++) {
			uint32_t arglen = htobe32(strlen(task->argv[j]));
			if(write(pipes.res_pipe, &arglen, sizeof(arglen)) < 0)
				return -1;
			if(write(pipes.res_pipe, task->argv[j], strlen(task->argv[j])) < 0)
				return -1;
		}
		free_task(task);
	}
	free(tasklist);
	close(pipes.res_pipe);
	return 0;
}

/**
* Function to handle the get stdout request
*/
int d_get_stdout (struct Pipes pipes, char * taskdir) {
	uint64_t taskid;

	if (read(pipes.req_pipe, &taskid, sizeof(uint64_t)) < 0)  {
		perror("[-] Error reading req_pipe");
		return (-1);
	}

	taskid = be64toh(taskid);
	struct Task *task;

	if(open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND))
		return -1;

	//load task
	if ((task = read_task_from_disk(taskdir, taskid)) == NULL) {
		perror("[-] Couldn't read task from directory");
		uint16_t er = htobe16(SERVER_REPLY_ERROR);
		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NOT_FOUND);
		if (write(pipes.res_pipe, &er, sizeof(er)) < 0)
			return -1;
		if (write(pipes.res_pipe, &nf, sizeof(nf)) < 0)
			return -1;
		return 0;
	}

	//check if the task was ever ran, if not send ERROR NOT FOUND
	if (task->nbruns < 1) {
		free_task(task);
		uint16_t er = htobe16(SERVER_REPLY_ERROR);
		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NEVER_RUN);
		if (write(pipes.res_pipe, &er, sizeof(er)) < 0)
			return -1;
		if (write(pipes.res_pipe, &nf, sizeof(nf)) < 0)
			return -1;
		return 0;
	}

	//send OK to cassini
	uint16_t ok = htobe16(SERVER_REPLY_OK);

	if (write(pipes.res_pipe, &ok, sizeof(ok)) < 0) {
		free_task(task);
		perror("[-] Failed to write OK");
		return -1;
	}

	//write the string length and stdout
	uint32_t outlen = htobe32((uint32_t) strlen(task -> std_out));
	if (write(pipes.res_pipe, &outlen, sizeof(uint32_t)) < 0){
		free_task(task);
		perror("[-] Failed to write stdout len");
		return -1;
	}

	if (write(pipes.res_pipe, task -> std_out, sizeof(char)*strlen(task->std_out)) < 0) {
		free_task(task);
		perror("[-] Failed to write stdout string");
		return -1;
	}


	close(pipes.res_pipe);
	free_task(task);
	return 0;
}

/**
* Function to handle the get stderr request
*/
int d_get_stderr (struct Pipes pipes, char * taskdir) {
	uint64_t taskid;

	if (read(pipes.req_pipe, &taskid, sizeof(uint64_t)) < 0)  {
		perror("[-] Error reading req_pipe");
		return (-1);
	}

	taskid = be64toh(taskid);
	struct Task *task;

	if(open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND))
		return -1;

	if ((task = read_task_from_disk(taskdir, taskid)) == NULL) {
		perror("[-] Couldn't read task from directory");
		uint16_t er = htobe16(SERVER_REPLY_ERROR);
		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NOT_FOUND);
		if (write(pipes.res_pipe, &er, sizeof(er)) < 0)
			return -1;
		if (write(pipes.res_pipe, &nf, sizeof(nf)) < 0)
			return -1;
		return 0;
	}

	if (task->nbruns < 1) {
		free_task(task);
		uint16_t er = htobe16(SERVER_REPLY_ERROR);
		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NEVER_RUN);
		if (write(pipes.res_pipe, &er, sizeof(er)) < 0)
			return -1;
		if (write(pipes.res_pipe, &nf, sizeof(nf)) < 0)
			return -1;
		return 0;
	}

	uint16_t ok = htobe16(SERVER_REPLY_OK);

	if (write(pipes.res_pipe, &ok, sizeof(ok)) < 0) {
		free_task(task);
		perror("[-] Failed to write OK");
		return -1;
	}

	uint32_t errlen = htobe32((uint32_t) strlen(task -> std_err));

	if (write(pipes.res_pipe, &errlen, sizeof(uint32_t)) < 0){
		free_task(task);
		perror("[-] Failed to write stderr len");
		return -1;
	}

	if (write(pipes.res_pipe, task -> std_err, sizeof(char)*strlen(task->std_err)) < 0) {
		free_task(task);
		perror("[-] Failed to write stderr string");
		return -1;
	}


	close(pipes.res_pipe);
	free_task(task);
	return 0;
}

/**
* Function to handle the remove task request
*/
int d_remove_task(struct Pipes pipes, char * tasks_dir) {
    uint64_t task_id;
	int task_id_size;
    char * task_dir;
    char * sub_entry;
    struct stat stat_path;
    struct dirent * entry;
 	DIR * dir;

	//read the taskid
    if (read(pipes.req_pipe, &task_id, sizeof(task_id)) < 0){
        perror("[-] d_remove_task: Error reading task_id");
        return (-1);
    }

	task_id = be64toh(task_id);
	task_id_size = ceil(log10(task_id + 1));
	task_dir = malloc(strlen(tasks_dir) + 1 + task_id_size + 1);

    sprintf(task_dir, "%s/%ld", tasks_dir, task_id);

	//if the task does not exist, send ERROR NOT FOUND to cassini
    if (stat(task_dir, &stat_path) < 0 || !S_ISDIR(stat_path.st_mode)) {
        perror("[-] Task does not exist / has no directory");

		if (open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND))
			return -1;

		uint16_t e = htobe16(SERVER_REPLY_ERROR);
		if (write(pipes.res_pipe, &e, sizeof(e)) < 0)
			return (-1);

		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NOT_FOUND);
		if (write(pipes.res_pipe, &nf, sizeof(nf)) < 0)
			return (-1);

		close(pipes.res_pipe);

        return (0);
    }

    if ((dir = opendir(task_dir)) == NULL) {
        perror("d_remove_task: [-] Error opening task directory");
        free(task_dir);
        return (-1);
    }

	//remove all files of the task
    while ((entry = readdir(dir)) != NULL) {
		char * entry_name = entry -> d_name;

        if (!strcmp(entry_name, ".") || !strcmp(entry_name, "..")) {
        	continue;
		}

		sub_entry = malloc(strlen(task_dir) + 1 + strlen(entry_name) + 1);

        sprintf(sub_entry, "%s/%s", task_dir, entry_name);

        if (unlink(sub_entry) != 0) {
            perror("d_remove_task: [-] Error deleting task files");
			closedir(dir);
			free(task_dir);
			free(sub_entry);
            return (-1);
        }

		free(sub_entry);
    }

	//remove the task folder
	if (rmdir(task_dir) != 0) {
		perror("d_remove_task: [-] Error removing task directory");
		free(task_dir);
		closedir(dir);
		return (-1);
	}

	if (open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND))
		return -1;

	uint16_t ok = htobe16(SERVER_REPLY_OK);
	if (write(pipes.res_pipe, &ok, sizeof(ok)) < 0)
		return (-1);

	close(pipes.res_pipe);
	closedir(dir);
    free(task_dir);
    return (0);
}

/**
* Function to handle the get times and exitcodes request
*/
int d_get_times_and_exit_codes (struct Pipes pipes, char * taskdir) {
	uint64_t taskid;

	if (read(pipes.req_pipe, &taskid, sizeof(uint64_t)) < 0)  {
		perror("[-] Error reading req_pipe");
		return (-1);
	}

	taskid = be64toh(taskid);
	struct Task *task;

	if(open_pipe(&pipes, REPLY_PIPE_ID, PIPE_OPEN_MODE_SATURND))
		return -1;

	//load task
	//if it doesn't exist send ERROR NOT FOUND to cassini
	if ((task = read_task_from_disk(taskdir, taskid)) == NULL) {
		puts("invalid task, sending NF");
		uint16_t er = htobe16(SERVER_REPLY_ERROR);
		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NOT_FOUND);
		int r1 = write(pipes.res_pipe, &er, sizeof(er));
		int r2 = write(pipes.res_pipe, &nf, sizeof(nf));
		if (r1 < 0 || r2 < 0)
			return -1;
		close(pipes.res_pipe);
		return 0;
	}

	if (task->nbruns <= 0) {
		uint16_t er = htobe16(SERVER_REPLY_ERROR);
		uint16_t nf = htobe16(SERVER_REPLY_ERROR_NEVER_RUN);
		int r1 = write(pipes.res_pipe, &er, sizeof(er));
		int r2 = write(pipes.res_pipe, &nf, sizeof(nf));
		if (r1 < 0 || r2 < 0)
			return -1;
		close(pipes.res_pipe);
		return 0;
	}

	//send OK to cassini
	uint16_t ok = htobe16(SERVER_REPLY_OK);

	if (write(pipes.res_pipe, &ok, sizeof(ok)) < 0) {
		free_task(task);
		perror("[-] Failed to write OK");
		return -1;
	}

	//send the number of runs
	uint32_t nbruns = htobe32(task->nbruns);
	if (write(pipes.res_pipe, &nbruns, sizeof(nbruns)) < 0) {
		free_task(task);
		perror("[-] Failed to write nbruns");
		return -1;
	}

	//send each time and exitcode
	for (int i = 0; i < task->nbruns; i++) {
		uint64_t ti = htobe64(task->times[i]);
		uint16_t ex = htobe16(task->exitcodes[i]);
		if (write(pipes.res_pipe, &ti, sizeof(ti)) <= 0) {
			free_task(task);
			perror("[-] Failed to write task->times");
			return -1;
		}
		if (write(pipes.res_pipe, &ex, sizeof(ex)) <= 0) {
			free_task(task);
			perror("[-] Failed to write task->exitcodes");
			return -1;
		}
	}
	free_task(task);
	close(pipes.res_pipe);
	return 0;
}
