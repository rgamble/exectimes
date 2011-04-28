/* Limit concurrent instances of the specified program.

   Copyright (C) 2011 Robert Gamble

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.  */

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PROGRAM_NAME "exectimes"
#define VERSION "1.0"
#define USAGE "Usage: " PROGRAM_NAME " lock-file max-instances command"
#define COPYRIGHT "Copyright (C) 2011 Robert Gamble\nLicense GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\nThis is free software: you are free to change and redistribute it.\nThere is NO WARRANTY, to the extent permitted by law."

typedef unsigned long lock_cnt_t;
#define lock_cnt_max ((lock_cnt_t)-1)
#define lock_cnt_size (sizeof(lock_cnt_t))

/*lint -esym(613, argv) */
int main(int argc, char * const argv[]) {
    int fd;
    struct flock fl;
    int check_instances = 0;
    int list_instances = 0;
    mode_t mask;
    lock_cnt_t i;
    lock_cnt_t max_idx = 0;
    lock_cnt_t max_allowed = 0;
    lock_cnt_t locks_held;
    lock_cnt_t first_free = 0;
    lock_cnt_t last_used = 0;

    if (argc == 1) {
        puts(PROGRAM_NAME " version " VERSION);
        puts(COPYRIGHT);
        puts(USAGE);
        exit(EXIT_FAILURE);
    }

    if (argc < 3) {
        puts(USAGE);
        exit(EXIT_FAILURE);
    }

    /* Open the lock file, create if doesn't exist */
    mask = umask(0);
    fd = open(argv[1], O_CREAT | O_RDWR, 0666); /*lint !e960*/
    if (fd < 0) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }
    (void) umask(mask);

    /* Process remaining arguments */
    if (strcmp(argv[2], "check") == 0) {
        check_instances = 1;
    }
    else if (strcmp(argv[2], "list") == 0) {
        list_instances = 1;
    }
    else {
        max_allowed = (lock_cnt_t) strtol(argv[2], NULL, 10);
    }

    if ((argc < 4) && (!check_instances) && (!list_instances)) {
        puts(USAGE);
        exit(EXIT_FAILURE);
    }

    /* Lock the beginning of the lock file */
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = lock_cnt_size;

    while (fcntl(fd, F_SETLKW, &fl) < 0) {
        if (errno == EINTR) {
            continue;
        }

        perror("Failed to lock file");
        exit(EXIT_FAILURE);
    }

    /* Read the max index */
    {
        const ssize_t result = read(fd, &max_idx, lock_cnt_size);
        if (result < 0) {
            perror("Failed to read file");
            exit(EXIT_FAILURE);
        }
        else if (result < (ssize_t) lock_cnt_size) {
            max_idx = lock_cnt_size;
        }
    }

    /* Get a count of the number of locks and the first available byte */
    locks_held = 0;
    for (i = lock_cnt_size; (i <= (max_idx + 1)) && (i < lock_cnt_max); i++) {
        fl.l_start = (off_t) i;
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_len = 1;

        if (fcntl(fd, F_GETLK, &fl) < 0) {
            perror("fcntl failure");
            exit(EXIT_FAILURE);
        }

        if (fl.l_type == F_UNLCK) {
            if (first_free == 0) {
                first_free = i;
            }
        }
        else {
            if (list_instances) {
                printf("Slot %lu held by PID %d\n",
                        (unsigned long) (i - lock_cnt_size) + 1,
                        (int) fl.l_pid);
            }
            locks_held++;
            last_used = i;
        }
    }

    if (list_instances) {
        exit(EXIT_SUCCESS);
    }

    if (check_instances) {
        printf("%lu instances running\n", locks_held);
        exit(EXIT_SUCCESS);
    }

    if ((locks_held >= max_allowed) || (first_free == 0)) {
        fprintf(stderr, "Cannot start, %lu instances already running\n", 
                (unsigned long) locks_held);
        exit(EXIT_FAILURE);
    }

    /* At least one lock is free, try to get it */
    fl.l_start = (off_t) first_free;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_len = 1;

    if (fcntl(fd, F_SETLK, &fl) < 0) {
        perror("Failed to lock previously determined free byte in file");
        exit(EXIT_FAILURE);
    }
   
    if (first_free > last_used) {
        last_used = first_free;
    }
    
    /* Update the max_idx byte if neccessary */
    if (last_used != max_idx) {
        lseek(fd, SEEK_SET, 0); /*lint !e747*/
        write(fd, &last_used, lock_cnt_size);
    }

    /* Unlock initial bytes and exec the new process */
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = lock_cnt_size;

    if (fcntl(fd, F_SETLK, &fl) < 0) {
        perror("Failed to release lock on file");
        exit(EXIT_FAILURE);
    }

    if (execvp(argv[3], &argv[3]) < 0) {
        perror("exec failed");
        exit(EXIT_FAILURE);
    }

    return EXIT_FAILURE;
}
