#include "tasks.h"
#include "timing-text-io.h"
#include "server-reply.h"
#include "timing.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>

/**
* Takes a task pointer and writes it to disk in the directory pointed by
* tasks_dir. Returns 0 on success and -1 on error.
*/
int write_task_to_disk(struct Task *task, char *tasks_dir) {
    struct stat st;
    char *current_task_dir = malloc(strlen(tasks_dir) + 16);

    sprintf(current_task_dir, "%s/%lu", tasks_dir, task->task_id);

    if (stat(current_task_dir, &st) == -1) {
        puts("[+] Creating task folder");
        mkdir(current_task_dir, 0700);
    }

	if (chdir(current_task_dir) != 0)
		return -1;

	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int perms =  S_IRUSR | S_IWUSR;
    int fexecs = open("execs", flags, perms);
    int fargc = open("argc", flags, perms);
    int fargv = open("argv", flags, perms);
    int fout = open("stdout", O_WRONLY | O_CREAT, perms);
    int ferr = open("stderr", O_WRONLY | O_CREAT, perms);
    int ftiming = open("timing", flags, perms);

    if(fargv == -1 || ftiming == -1 || fexecs == -1 || fout == -1 || ferr == -1) {
        perror("[-] Error opening one of task files for writing");
        return -1;
    }

    if (write(fargc, &task->argc, sizeof(uint32_t)) < 0)
        return -1;

    for (int i = 0; i < task->argc; i++) {
        uint16_t arglen = strlen(task->argv[i]);
        if (write(fargv, &arglen, sizeof(uint16_t)) < 0)
            return -1;
        if (write(fargv, task->argv[i], arglen) < 0)
            return -1;
    }

    if (write(ftiming, &task -> timing.minutes, sizeof(uint64_t)) < 0) {
        return -1;
    }
    if (write(ftiming, &task -> timing.hours, sizeof(uint32_t)) < 0) {
        return -1;
    }
    if (write(ftiming, &task -> timing.daysofweek, sizeof(uint8_t)) < 0) {
        return -1;
    }

    if (write(fexecs, &task->nbruns, sizeof(task->nbruns)) < 0)
        return -1;

    for (int i = 0; i < task->nbruns; i++) {
		uint64_t time = task->times[i];
		uint16_t exitcode = task->exitcodes[i];
        if (write(fexecs, &time, sizeof(uint64_t)) < 0)
            return -1;
        if (write(fexecs, &exitcode, sizeof(uint16_t)) < 0)
            return -1;

    }

	free(current_task_dir);
	close(fexecs);
    close(fargc);
	close(fargv);
	close(fout);
	close(ferr);
	close(ftiming);
    return 0;
}

/**
* Reads the task with identifier taskid in the directory pointed by tasks_dir.
* A pointer to the task is returned. The task must be freed later.
* Returns NULL on error.
*/
struct Task * read_task_from_disk(char *tasks_dir, uint64_t taskid) {
    struct stat st;
    struct Task *task;
    
    //initial task size
    int task_size = sizeof(uint64_t) +		//taskid
					sizeof(uint32_t) +		//argc
					sizeof(char **) +		//argv
					sizeof(struct timing) +	//timing
					sizeof(uint16_t *) +	//exitcodes arrays
					sizeof(uint64_t *) +	//times array
					sizeof(uint32_t) +		//nbruns
					2 * sizeof(char *)  	//stdout & stderr
                    + 8                     //task struct
	;
    uint32_t argc;
    char **argv = NULL;
    struct timing *t;
    uint16_t *exitcodes = NULL;
    uint64_t *times = NULL;
    uint32_t nbruns;
    char *stdout_buf;
    char *stderr_buf;

    char *current_task_dir = malloc(strlen(tasks_dir) + sizeof(uint64_t));

    sprintf(current_task_dir, "%s/%lu", tasks_dir, taskid);

    if (stat(current_task_dir, &st) == -1) {
		free(current_task_dir);
        perror("[-] read_task_from_disk : Can't read task folder");
        return NULL;
    }

    if (chdir(current_task_dir) != 0) {
		free(current_task_dir);
		return NULL;
	}

    int fexecs = open("execs", O_RDONLY);
    int fargc = open("argc", O_RDONLY);
    int fargv = open("argv", O_RDONLY);
    int fout = open("stdout", O_RDONLY);
    int ferr = open("stderr", O_RDONLY);
    int ftiming = open("timing", O_RDONLY);

    if(fargv == -1 || ftiming == -1 || fexecs == -1 || fout == -1 || ferr == -1) {
		free(current_task_dir);
        perror("[-] Error opening one of task files for reading");
        return NULL;
    }

    //read nbruns and each time/exitcodes
    if (read(fexecs, &nbruns, sizeof(nbruns)) <= 0)
        return NULL;
    if (nbruns > 0) {
        times = malloc(nbruns * sizeof(uint64_t));
        exitcodes = malloc(nbruns * sizeof(uint16_t));
        task_size += nbruns * sizeof(uint64_t) + nbruns * sizeof(uint16_t);
        for (int i = 0; i < nbruns; i++) {
            uint64_t time_val;
            uint16_t exitcode_val;
            if (read(fexecs, &time_val, sizeof(time_val)) <= 0)
                return NULL;
            if (read(fexecs, &exitcode_val, sizeof(exitcode_val)) <= 0)
                return NULL;
            times[i] = time_val;
            exitcodes[i] = exitcode_val;
        }
    }

    //read the timing structure
    t = malloc(sizeof(struct timing));
    if(read(ftiming, &t->minutes, sizeof(uint64_t)) <= 0)
        return NULL;
    if(read(ftiming, &t->hours, sizeof(uint32_t)) <= 0)
        return NULL;
    if(read(ftiming, &t->daysofweek, sizeof(uint8_t)) <= 0)
        return NULL;

    //read the argc and each argv
    if(read(fargc, &argc, sizeof(uint32_t)) <= 0)
        return NULL;
    argv = malloc((argc+1) * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        uint16_t arglen;
        if (read(fargv, &arglen, sizeof(uint16_t)) <= 0)
            return NULL;
        argv[i] = malloc(arglen + 1);
        task_size += arglen + 1;
        if (read(fargv, argv[i], arglen) <= 0) {
            perror("arg read error");
            return NULL;
        }
        argv[i][arglen] = 0;
    }
    argv[argc] = (char*)NULL;

    //read stdout
    fstat(fout, &st);
    stdout_buf = malloc(st.st_size + 1);
    task_size += st.st_size + 1;
    lseek(fout, 0, SEEK_SET);
    if(read(fout, stdout_buf, st.st_size) < 0)
        return NULL;
    stdout_buf[st.st_size] = 0;

    //read stderr
    fstat(ferr, &st);
    stderr_buf = malloc(st.st_size + 1);
    task_size += st.st_size + 1;
    lseek(ferr, 0, SEEK_SET);
    if(read(ferr, stderr_buf, st.st_size) < 0)
        return NULL;
    stderr_buf[st.st_size] = 0;

    task = malloc(task_size);

    task->task_id = taskid;
    task->argc = argc;
    task->argv = argv;
    task->timing = *t;
    task->nbruns = nbruns;
    task->times = times;
    task->exitcodes = exitcodes;
    task->std_err = stderr_buf;
    task->std_out = stdout_buf;

    free(current_task_dir);
    free(t);
    close(fexecs);
    close(fargc);
	close(fargv);
	close(fout);
	close(ferr);
	close(ftiming);
    return task;
}

/**
* Loads all tasks from the directory pointed by task_dir. Write the number of
* loaded tasks in taskcount. A task is defined as being a subdirectory with
* a name for a number. This function does not check for file types and assumes
* the task_dir was the one created by saturnd and hasn't been tampered with.
*/
struct Task ** load_all_tasks(char *task_dir, uint32_t *taskcount) {
    DIR * dirp;
    struct dirent * dirent;
    struct Task **tasklist;
    *taskcount = 0;
    errno = 0;

    //go through the directory once to count the number of tasks
    dirp = opendir(task_dir);
    if (dirp == NULL) {
        perror("[-] Error opening tasks directory");
        return NULL;
    }
    while ((dirent = readdir(dirp)) != NULL) {
        int taskid = atoi(dirent->d_name);
        if (taskid != 0)
          (*taskcount)++;
    }
    if (errno != 0) {
        perror("[-] Error counting tasks");
        return NULL;
    }
    closedir(dirp);

    //allocate a task list with the correct size
    tasklist = malloc(sizeof(struct Task *) * (*taskcount));
    if (tasklist == NULL) {
        perror("malloc");
        return NULL;
    }
    
    //go through the directory again to load all tasks
    dirp = opendir(task_dir);
    if (dirp == NULL) {
        perror("[-] Error opening tasks directory");
        return NULL;
    }
    int counter = 0;
    while ((dirent = readdir(dirp)) != NULL) {
        int taskid = atoi(dirent->d_name);
        if (taskid == 0)
            continue;
        tasklist[counter] = read_task_from_disk(task_dir, taskid);
        if (tasklist[counter] == NULL) {
            perror("[-] Error reading task");
            return NULL;
        }
        counter++;
    }
    if (errno != 0) {
        perror("Error reading task directory content");
        return NULL;
    }
    
    closedir(dirp);
    free(dirent);
    return tasklist;
}

/**
* Frees a task structure and all its attributes
*/
void free_task(struct Task *task) {
    free(task->std_out);
    free(task->std_err);
    free(task->times);
    free(task->exitcodes);
    for (int i=0; i<task->argc; i++)
        free(task->argv[i]);
    free(task->argv);
    free(task);
}
