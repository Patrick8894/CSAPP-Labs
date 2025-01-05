#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

typedef void handler_t(int);

/* Global variables */
extern char **environ;      /* defined in libc */
int verbose = 0;  
int nextjid = 1;            /* next job ID to allocate */
char prompt[] = "tsh> ";
int emit_prompt = 1; /* emit prompt (default) */

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);

/* Define the job struct */
typedef struct {
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
} job_t;
job_t jobs[MAXJOBS]; /* The job list */

void init_jobs(job_t *jobs);
job_t *get_job_by_jid(int jid);
job_t *get_job_by_pid(pid_t pid);
int addjob(pid_t pid, int state, char *cmdline);
int deletejob(pid_t pid);
void clearjob(job_t *job);
int maxjid(job_t *jobs);

handler_t *Signal(int signum, handler_t *handler);
void eval(char *cmdline);
int parseline(const char *cmdline, char **argv);
void waitfg(pid_t pid);
pid_t fgpid(job_t *jobs);
void listjobs();
void do_bgfg(char **argv);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	    default:
            usage();
	    }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    init_jobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed
 */
void eval(char *cmdline)
{
    char *argv[MAXARGS]; // Argument list execve()
    char buf[MAXLINE];   // Holds modified command line
    int bg;              // Should the job run in bg or fg?
    pid_t pid;           // Process id

    strcpy(buf, cmdline);
    bg = parseline(buf, argv); // Parse the command line
    if (argv[0] == NULL) {
        return; // Ignore empty lines
    }

    sigset_t mask_all, prev_one;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_one); // Block 

    if (!strcmp(argv[0], "quit")) { // Quit command
        exit(0);
    } else if (!strcmp(argv[0], "jobs")) { // Jobs command
        listjobs();
    } else if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) { // Background or Foreground command
        do_bgfg(argv);
    } else if (!strcmp(argv[0], "&")) { // Ignore singleton &
        sigprocmask(SIG_SETMASK, &prev_one, NULL); // Unblock signals
        return;
    } else {
        if ((pid = fork()) == 0) { // Child process
            setpgid(0, 0); // Put the child in a new process group
            sigprocmask(SIG_SETMASK, &prev_one, NULL); // Unblock signals
            if (execvp(argv[0], argv) < 0) {
                printf("%s: Command not found\n", argv[0]);
                exit(0);
            }
        }

        addjob(pid, bg ? BG : FG, cmdline); // Add the job to the job list

        if (!bg) {
            waitfg(pid); // Wait for foreground job to terminate
        } else {
            printf("[%d] (%d) %s", get_job_by_pid(pid)->jid, pid, cmdline);
        }
    }

    sigprocmask(SIG_SETMASK, &prev_one, NULL); // Unblock signals
    return;
}

/*
 * parseline - Parse the command line and build the argv array
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; // Holds local copy of command line
    char *buf = array;          // Pointer to traverse the command line
    char *delim;                // Points to first space delimiter
    int argc;                   // Number of arguments
    int bg;                     // Background job?

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' '; // Replace trailing '\n' with space
    while (*buf && (*buf == ' ')) // Ignore leading spaces
        buf++;

    // Build the argv list
    argc = 0;
    while ((delim = strchr(buf, ' '))) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) // Ignore spaces
            buf++;
    }
    argv[argc] = NULL;

    if (argc == 0) // Ignore blank line
        return 1;

    // Should the job run in the background?
    if ((bg = (*argv[argc - 1] == '&')) != 0)
        argv[--argc] = NULL;

    return bg;
}

/*
 * listjobs - List all jobs
 */
void listjobs() 
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                           i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    job_t *jobp = NULL;
    int jid;
    pid_t pid;
    char *id = argv[1];

    if (id == NULL) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    // check the format of the argument
    if (id[0] != '%' && !isdigit(id[0])) {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    for (int i = 1; i < strlen(id); i++) {
        if (!isdigit(id[i])) {
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
        }
    }

    if (id[0] == '%') { // Job ID
        jid = atoi(&id[1]);
        jobp = get_job_by_jid(jid);
        if (jobp == NULL) {
            printf("%s: No such job\n", id);
            return;
        }
    } else if (isdigit(id[0])) { // Process ID
        pid = atoi(id);
        jobp = get_job_by_pid(pid);
        if (jobp == NULL) {
            printf("(%d): No such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    // Continue the job
    kill(-(jobp->pid), SIGCONT);

    // Update job state
    if (!strcmp(argv[0], "bg")) {
        jobp->state = BG;
        printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
    } else if (!strcmp(argv[0], "fg")) {
        jobp->state = FG;
        waitfg(jobp->pid);
    } else {
        printf("do_bgfg: Internal error\n");
    }
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    sigset_t mask;
    sigemptyset(&mask);

    while (pid == fgpid(jobs)) {
        sigsuspend(&mask);
    }
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 * user types ctrl-c at the keyboard. Catch it and send it along
 * to the foreground job.
 */
void sigint_handler(int sig) 
{
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    pid_t pid;

    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all); // Block all signals

    write(STDOUT_FILENO, "INTERRUPT\n", 10);

    pid = fgpid(jobs);
    if (pid != 0) {
        kill(-pid, sig); // Send SIGINT to the foreground job's process group
    }

    if (emit_prompt == 1) {
        write(STDOUT_FILENO, prompt, strlen(prompt));
    }

    fflush(stdout);

    errno = olderrno;
    sigprocmask(SIG_SETMASK, &prev_all, NULL); // Restore previous signal mask
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 * the user types ctrl-z at the keyboard. Catch it and suspend the
 * foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig) 
{
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    pid_t pid;

    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all); // Block all signals

    write(STDOUT_FILENO, "STOP\n", 5);

    pid = fgpid(jobs);
    if (pid != 0) {
        kill(-pid, sig); // Send SIGTSTP to the foreground job's process group
    }

    if (emit_prompt == 1) {
        write(STDOUT_FILENO, prompt, strlen(prompt));
    }

    fflush(stdout);

    errno = olderrno;
    sigprocmask(SIG_SETMASK, &prev_all, NULL); // Restore previous signal mask
}

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 * a child job terminates (becomes a zombie), or stops because it
 * received a SIGSTOP or SIGTSTP signal. The handler reaps all
 * available zombie children, but doesn't wait for any other
 * currently running children to terminate.
 */
void sigchld_handler(int sig) 
{
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    pid_t pid;
    int status;
    job_t *job;

    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all); // Block all signals

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job = get_job_by_pid(pid);
        if (WIFEXITED(status)) {
            deletejob(pid); // Delete the job if it terminated normally
        } else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal %d\n", job->jid, pid, WTERMSIG(status));
            deletejob(pid); // Delete the job if it was terminated by a signal
        } else if (WIFSTOPPED(status)) {
            job_t *job = get_job_by_pid(pid);
            if (job != NULL) {
                job->state = ST; // Mark the job as stopped
                printf("Job [%d] (%d) stopped by signal %d\n", job->jid, pid, WSTOPSIG(status));
            }
        }
    }

    errno = olderrno;
    sigprocmask(SIG_SETMASK, &prev_all, NULL); // Restore previous signal mask
}

/*
 * fgpid - Return PID of current foreground job, 0 if no such job
 */
pid_t fgpid(job_t *jobs)
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state == FG) {
            return jobs[i].pid;
        }
    }
    return 0;
}

void init_jobs(job_t *jobs) 
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        jobs[i].pid = 0;
        jobs[i].jid = 0;
        jobs[i].state = UNDEF;
        jobs[i].cmdline[0] = '\0';
    }
}

/*
 * get_job_by_jid - Find a job (job_t) with a given job ID (jid)
 */
job_t *get_job_by_jid(int jid) 
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid == jid) {
            return &jobs[i];
        }
    }
    return NULL; // Job not found
}

/*
 * get_job_by_pid - Find a job (job_t) with a given process ID (pid)
 */
job_t *get_job_by_pid(pid_t pid) 
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL; // Job not found
}

/*
 * addjob - Add a job to the job list
 */
int addjob(pid_t pid, int state, char *cmdline) 
{
    int i;
    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose) {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/*
 * deletejob - Delete a job whose PID=pid from the job list
 */
int deletejob(pid_t pid)
{
    int i;
    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/*
 * clearjob - Clear the entries in a job struct
 */
void clearjob(job_t *job) 
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/*
 * maxjid - Returns the largest allocated job ID
 */
int maxjid(job_t *jobs) 
{
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    }
    return max;
}

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}