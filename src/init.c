// SPDX-License-Identifier: Apache-2.0
/*
 * OmniQ-OS: Native C Userspace PID 1 Initializer (src/init.c)
 * Used in minimal embedded cryogenic rack appliance ISO (omniq-os.iso)
 * or as fast C bootstrapper delegating to /etc/omniq/omniq-system-init.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void reap_zombies(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main(void) {
    if (getpid() != 1) {
        printf("[omniq-init] Userspace C PID 1 binary verified (non-PID1 invocation).\n");
        return 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = reap_zombies;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    mkdir("/proc", 0555);
    mkdir("/sys", 0555);
    mkdir("/dev", 0755);
    mkdir("/run", 0755);
    mkdir("/tmp", 01777);

    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mkdir("/dev/pts", 0755);
    mkdir("/dev/shm", 01777);
    mount("devpts", "/dev/pts", "devpts", 0, "gid=5,mode=620,ptmxmode=666");
    mount("tmpfs", "/dev/shm", "tmpfs", 0, "mode=1777,size=512M");
    mount("tmpfs", "/run", "tmpfs", 0, "mode=0755");

    int cfd = open("/dev/console", O_RDWR);
    if (cfd >= 0) {
        dup2(cfd, STDIN_FILENO);
        dup2(cfd, STDOUT_FILENO);
        dup2(cfd, STDERR_FILENO);
        if (cfd > 2) close(cfd);
    }

    sethostname("omniq-rt", 8);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", "linux", 1);

    /* Load kernel accelerator driver if bundled */
    mkdir("/dev/accel", 0755);
    if (access("/opt/omniq/drivers/omniq_qpu.ko", R_OK) == 0) {
        pid_t p = fork();
        if (p == 0) {
            execl("/sbin/insmod", "insmod", "/opt/omniq/drivers/omniq_qpu.ko", (char *)NULL);
            _exit(1);
        }
        waitpid(p, NULL, 0);
    }

    /* Delegate to full system init if present */
    if (access("/etc/omniq/omniq-system-init", X_OK) == 0) {
        execl("/etc/omniq/omniq-system-init", "/etc/omniq/omniq-system-init", (char *)NULL);
    }

    printf("\n====================================================================\n");
    printf(" OmniQ-OS Real-Time Cryogenic Quantum Appliance (Native C PID 1)    \n");
    printf(" Kernel Accelerator: /dev/accel/qpu0 | QEC Loop < 600 ns            \n");
    printf("====================================================================\n\n");

    if (access("/usr/local/bin/qpu-smi", X_OK) == 0) {
        pid_t smi = fork();
        if (smi == 0) {
            execl("/usr/local/bin/qpu-smi", "qpu-smi", (char *)NULL);
            _exit(0);
        }
        waitpid(smi, NULL, 0);
    }

    /* Spawn serial rescue console on ttyS0 if present */
    if (access("/dev/ttyS0", R_OK | W_OK) == 0) {
        if (fork() == 0) {
            int sfd = open("/dev/ttyS0", O_RDWR);
            if (sfd >= 0) {
                setsid();
                dup2(sfd, 0); dup2(sfd, 1); dup2(sfd, 2);
                if (sfd > 2) close(sfd);
                execl("/bin/sh", "-sh", (char *)NULL);
            }
            _exit(0);
        }
    }

    while (1) {
        pid_t sh = fork();
        if (sh == 0) {
            setsid();
            execl("/bin/sh", "-sh", (char *)NULL);
            _exit(1);
        }
        waitpid(sh, NULL, 0);
        sleep(1);
    }
    return 0;
}
