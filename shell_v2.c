/*
 * shell.c
 * Team SJP - COSC 4348
 * Simple command-line shell - Week 2
 * Features: Input loop, command parsing, basic execution,
 *           background processes (&), expanded built-ins (pwd, jobs)
 * Week 2 additions by: Sanskar Chaudhari
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT   1024
#define MAX_ARGS    64
#define MAX_BGPROCS 64
#define PROMPT      "sjp-shell$ "

/* Track background processes */
typedef struct {
    pid_t pid;
    char  cmd[256];
} BgProc;

BgProc bg_procs[MAX_BGPROCS];
int    bg_count = 0;

/* Function prototypes */
void    run_loop();
char   *read_input();
char  **parse_input(char *input, int *background);
int     execute(char **args, int background);
int     handle_builtin(char **args);
void    check_bg_procs();
void    add_bg_proc(pid_t pid, char *cmd);
void    remove_bg_proc(pid_t pid);

int main() {
    run_loop();
    return 0;
}

/* ─── Main Loop ─────────────────────────────────────────────────────────── */

void run_loop() {
    char *input;
    char **args;
    int   running    = 1;
    int   background = 0;

    while (running) {
        /* check if any background processes finished */
        check_bg_procs();

        printf("%s", PROMPT);
        fflush(stdout);

        input = read_input();

        /* skip empty input */
        if (input == NULL || strlen(input) == 0) {
            free(input);
            continue;
        }

        args = parse_input(input, &background);

        if (args[0] != NULL) {
            running = execute(args, background);
        }

        free(input);
        free(args);
    }
}

/* ─── Read Input ────────────────────────────────────────────────────────── */

char *read_input() {
    char *buffer = malloc(MAX_INPUT);
    if (!buffer) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    if (fgets(buffer, MAX_INPUT, stdin) == NULL) {
        free(buffer);
        return NULL;
    }

    /* strip newline */
    buffer[strcspn(buffer, "\n")] = '\0';

    return buffer;
}

/* ─── Parse Input ───────────────────────────────────────────────────────── */

/*
 * Parses user input into tokens.
 * Sets *background = 1 if the last token is '&', then removes it.
 */
char **parse_input(char *input, int *background) {
    *background = 0;

    char **args = malloc(MAX_ARGS * sizeof(char *));
    if (!args) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    int i = 0;
    char *token = strtok(input, " \t");

    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;

    /* check if last argument is '&' */
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        *background  = 1;
        args[i - 1]  = NULL; /* remove '&' from args */
    }

    return args;
}

/* ─── Execute ───────────────────────────────────────────────────────────── */

int execute(char **args, int background) {
    /* check for built-in commands first */
    int builtin_result = handle_builtin(args);
    if (builtin_result != -1) {
        return builtin_result;
    }

    /* fork and exec for external commands */
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {
        /* child process */
        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "sjp-shell: command not found: %s\n", args[0]);
            exit(EXIT_FAILURE);
        }
    } else {
        if (background) {
            /* background: don't wait, just track it */
            printf("[%d] running in background\n", pid);
            add_bg_proc(pid, args[0]);
        } else {
            /* foreground: wait for child to finish */
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return 1;
}

/* ─── Background Process Tracking ──────────────────────────────────────── */

/* Add a new background process to the list */
void add_bg_proc(pid_t pid, char *cmd) {
    if (bg_count < MAX_BGPROCS) {
        bg_procs[bg_count].pid = pid;
        strncpy(bg_procs[bg_count].cmd, cmd, 255);
        bg_procs[bg_count].cmd[255] = '\0';
        bg_count++;
    }
}

/* Remove a finished background process from the list */
void remove_bg_proc(pid_t pid) {
    for (int i = 0; i < bg_count; i++) {
        if (bg_procs[i].pid == pid) {
            for (int j = i; j < bg_count - 1; j++) {
                bg_procs[j] = bg_procs[j + 1];
            }
            bg_count--;
            return;
        }
    }
}

/* Check if any background processes have finished */
void check_bg_procs() {
    int status;
    pid_t pid;

    /* WNOHANG = don't block, just check */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("\n[%d] done\n", pid);
        remove_bg_proc(pid);
    }
}

/* ─── Built-in Commands ─────────────────────────────────────────────────── */

/*
 * Returns:  0  -> exit shell
 *           1  -> keep running
 *          -1  -> not a built-in (handle externally)
 */
int handle_builtin(char **args) {
    if (strcmp(args[0], "exit") == 0) {
        printf("Exiting sjp-shell. Goodbye!\n");
        return 0;
    }

    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "sjp-shell: cd: missing argument\n");
        } else if (chdir(args[1]) != 0) {
            perror("cd failed");
        }
        return 1;
    }

    /* NEW: pwd built-in */
    if (strcmp(args[0], "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd failed");
        }
        return 1;
    }

    /* NEW: jobs built-in - list running background processes */
    if (strcmp(args[0], "jobs") == 0) {
        if (bg_count == 0) {
            printf("No background processes running.\n");
        } else {
            printf("\nBackground processes:\n");
            for (int i = 0; i < bg_count; i++) {
                printf("  [%d]  %s\n", bg_procs[i].pid, bg_procs[i].cmd);
            }
            printf("\n");
        }
        return 1;
    }

    if (strcmp(args[0], "help") == 0) {
        printf("\n=== SJP Shell - Help ===\n");
        printf("Built-in commands:\n");
        printf("  cd <dir>  - change directory\n");
        printf("  pwd       - print current directory\n");
        printf("  jobs      - list background processes\n");
        printf("  help      - show this message\n");
        printf("  exit      - exit the shell\n");
        printf("Tip: add & at the end of any command to run it in the background.\n\n");
        return 1;
    }

    return -1; /* not a built-in */
}
