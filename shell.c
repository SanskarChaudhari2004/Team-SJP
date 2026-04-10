/*
 * shell.c
 * Team SJP - COSC 4348
 * Simple command-line shell - Week 1
 * Features: Input loop, command parsing, basic execution
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT  1024
#define MAX_ARGS   64
#define PROMPT     "sjp-shell$ "

/* Function prototypes */
void run_loop();
char *read_input();
char **parse_input(char *input);
int execute(char **args);
int handle_builtin(char **args);

int main() {
    run_loop();
    return 0;
}

/* ─── Main Loop ─────────────────────────────────────────────────────────── */

void run_loop() {
    char *input;
    char **args;
    int running = 1;

    while (running) {
        printf("%s", PROMPT);
        fflush(stdout);

        input = read_input();

        /* skip empty input */
        if (input == NULL || strlen(input) == 0) {
            free(input);
            continue;
        }

        args = parse_input(input);

        if (args[0] != NULL) {
            running = execute(args);
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

char **parse_input(char *input) {
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

    args[i] = NULL; /* null-terminate the array */
    return args;
}

/* ─── Execute ───────────────────────────────────────────────────────────── */

int execute(char **args) {
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
        /* parent process - wait for child */
        int status;
        waitpid(pid, &status, 0);
    }

    return 1;
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

    if (strcmp(args[0], "help") == 0) {
        printf("\n=== SJP Shell - Help ===\n");
        printf("Built-in commands:\n");
        printf("  cd <dir>  - change directory\n");
        printf("  help      - show this message\n");
        printf("  exit      - exit the shell\n");
        printf("Any other command is passed to the OS.\n\n");
        return 1;
    }

    return -1; /* not a built-in */
}
