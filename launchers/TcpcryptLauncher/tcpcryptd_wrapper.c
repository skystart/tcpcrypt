#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

void setup_ipfw_rules();
void run_tcpcryptd(char *);
void stop_tcpcryptd();
void teardown_ipfw_rules();

static const char *pidfile = "/private/tmp/tcpcrypt.pid";

static char work_dir[4096];

void setup_ipfw_rules() {
        char pf[4096];
	char *ipfw_cmds[] = {
                "mkdir -p /var/run/tcpcryptd",
                "dscl . create /Users/tcpcryptd UniqueID 666",
                "dscl . create /Users/tcpcryptd PrimaryGroupID 666",
		pf,
		NULL
	};
	int i;

        snprintf(pf, sizeof(pf), "pfctl -Fa -e -f %s/pf.conf", work_dir);
	
	printf("Setting up ipfw rules...\n");
	for (i = 0; ipfw_cmds[i] != NULL; ++i) {
		if (system(ipfw_cmds[i]))
			err(1, "%s", ipfw_cmds[i]);
	}
}

void run_tcpcryptd(char *my_argv0) {
	int fd;
	FILE *file;
	struct stat st;
	char tcpcryptd[4096];

	/* stop tcpcryptd if it's running */
	stop_tcpcryptd();
	
	/* save pid */
	fd = open(pidfile, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, 0600);
	if (fd == -1)
		err(1, "open()");
	if (fstat(fd, &st) == -1)
		err(1, "fstat()");
	if (!(st.st_mode & S_IFREG))
		errx(1, "pidfile not regular file");
	if (fchmod(fd, 0600) == -1)
		err(1, "fchmod()");
	if (fchown(fd, 0, 0) == -1)
		err(1, "fchown()");
	if (!(file = fdopen(fd, "w")))
		err(1, "fdopen()");
	if (fprintf(file, "%d", getpid()) < 1)
		err(1, "fprintf()");
	if (fclose(file))
		err(1, "fclose()");
	
        snprintf(tcpcryptd, sizeof(tcpcryptd), "%s/tcpcryptd", work_dir);

	printf("Starting tcpcryptd...\n");
	if (execl(tcpcryptd, "tcpcryptd", "-e", "-u", ":65531", NULL) == -1)
		err(1, "execve()");
}

void stop_tcpcryptd() {
	struct stat st;
	int fd;
	FILE *file;
	pid_t tcpcryptd_pid;
	
	fd = open(pidfile, O_RDONLY | O_NOFOLLOW);
	if (fd == -1) {
		if (errno == ENOENT) {
			return;
		} else {
			err(1, "open()");
		}
	}
	
	if (fstat(fd, &st) == -1)
		err(1, "fstat()");
	
	/* check pidfile perms/attrs are safe */
	int regfile = st.st_mode & S_IFREG;
	int rootowned = st.st_uid == 0 && st.st_gid == 0;
	int othernorw = (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
	if (!regfile || !rootowned || !othernorw)
		errx(1, "bad perms/attrs on pidfile");
	
	/* unlink pidfile */
	if (fchmod(fd, 0600) == -1)
		err(1, "fchmod()");
	if (fchown(fd, 0, 0) == -1)
		err(1, "fchown()");
	if (unlink(pidfile) == -1)
		err(1, "unlink()");
	
	if (!(file = fdopen(fd, "r")))
		err(1, "fdopen()");
	if (fscanf(file, "%d", &tcpcryptd_pid) != 1)
		errx(1, "fscanf: no pid");
	
	if (tcpcryptd_pid <= 0)
		errx(1, "invalid pid %d", tcpcryptd_pid);
	
	if (kill(tcpcryptd_pid, SIGTERM) == -1)
		err(1, "kill(%d)", tcpcryptd_pid);
	
	if (fclose(file) != 0)
		err(1, "fclose()");
}

void teardown_ipfw_rules() {
	static char *cmd = "pfctl -d";
	
	printf("Restoring ipfw to previous configuration...");
	if (system(cmd))
		warn("ipfw warning: %s", cmd);
	printf("OK\n");	
}

int main(int argc, char **argv) {
	static char *start = "start", *stop = "stop";
	char *action = argv[1];
        char *p;

        snprintf(work_dir, sizeof(work_dir), "%s", argv[0]);
        p = strrchr(work_dir, '/');
        if (p)
            *p = 0;

        printf("Work dir: [%s]\n", work_dir);

	if (setuid(0) != 0) {
		printf("must be root\n");
		exit(1);
	}
	
	if (strncmp(action, start, strlen(start)) == 0) {
		setup_ipfw_rules();
		run_tcpcryptd(argv[0]);
	} else if (strncmp(action, stop, strlen(stop)) == 0) {
		teardown_ipfw_rules();
		stop_tcpcryptd();
	} else {
		printf("usage: %s start|stop\n", argv[0]);
		exit(1);
	}
	
	return 0;
}
