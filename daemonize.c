/*
 * Name: daemonize.cpp
 *
 * Version: 1.0
 *
 * Purpose: 用于将程序作为守护进程运行的一些接口函数
 *
 * Created by: Tangzongsheng
 *
 * UNICATION CO., LTD PROPRIETARY INFORMATION
 *
 * SECURITY LEVEL - HIGHLY CONFIDENTIAL
 *
 * DO NOT COPY
 *
 * This document and the information contained in it is confidential and
 * proprietary to Unication Co., Ltd. The reproduction or disclosure, in
 * whole or in part, to anyone outside of Unication Co., Ltd. without the
 * written approval of the President of Unication Co., Ltd., under a
 * Non-Disclosure Agreement, or to any employee of Unication Co., Ltd. who
 * has not previously obtained written authorization for access from the
 * individual responsible for the document, will have a significant
 * detrimental effect on Unication Co., Ltd. and is expressly prohibited.
 * * Copyright (c) $Date: 2011/10/26 07:10:19 $ Unication Co., Ltd., Inc.
 *
 * All rights reserved
 */


#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#include "daemonize.h"

extern void log2file(const char *fmt, ...);

void daemonize(const char *cmd)
{
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct rlimit rl;
    struct sigaction sa;

    /*
     * Clear file creation mask.
     */
    umask(0);

    /*
     * Get maximum number of file descriptors.
     */
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        log2file("%s: can't get file limit\n", cmd);
        exit(1);
    }

    /*
     * Become a session leader to lose controlling TTY.
     */
    if ((pid = fork()) < 0) {
        log2file("%s: can't fork\n", cmd);
        exit(1);
    } else if (pid != 0) /* parent */ {
        exit(0);
    }
    
    setsid();

    /*
     * Ensure future opens won't allocate controlling TTYs.
     */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        log2file("%s: can't ignore SIGHUP\n", cmd);
        exit(1);
    }
#if 0
    if ((pid = fork()) < 0) {
        log2file("%s: can't fork\n", cmd);
        exit(1);
    } else if (pid != 0) /* parent */ {
        exit(0);
    }
#endif
    /*
     * Change the current working directory to the root so
     * we won't prevent file systems from being unmounted.
     */
    if (chdir("/") < 0) {
        log2file("%s: can't change directory to /\n");
        exit(1);
    }

    /*
     * Close all open file descriptors.
     */
    if (rl.rlim_max == RLIM_INFINITY) {
        rl.rlim_max = 1024;
    }
    
    for (i = 0; (unsigned int)i < rl.rlim_max; i++) {
        close(i);
    }

    /*
     * Attach file descriptors 0, 1, and 2 to /dev/null.
     */
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);

    /*
     * Initialize the log file.
     */
    openlog(cmd, LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        log2file("unexpected file descriptors\n");
        exit(1);
    }
}

#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)

int lockfile(int fd)
{
    struct flock fl;

    fl.l_type = F_WRLCK;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    return(fcntl(fd, F_SETLK, &fl));
}

int isAlreadyRunning()
{
   int fd;
   char buf[16];

   fd = open(LOCKFILE, O_RDWR|O_CREAT, LOCKMODE);
   if (fd < 0) {
       log2file("can't open %s: %s\n", LOCKFILE, strerror(errno));
       exit(1);
   }

   if (lockfile(fd) < 0){
       if (errno == EACCES || errno == EAGAIN) {
           close(fd);
           return(1);
       }
       log2file("can't lock %s: %s\n", LOCKFILE, strerror(errno));
       exit(1);
   }
   if (ftruncate(fd, 0)) {} 
   sprintf(buf, "%ld", (long)getpid());
   if (write(fd, buf, strlen(buf)+1)) {}
   return (0);
}

