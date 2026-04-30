/*
 * shell_v2.c
 * Team SJP - COSC 4348
 * Simple command-line shell
 * Features: command parsing, built-ins, background execution, signal handling,
 *           I/O redirection, pipelines, and batch mode
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT      1024
#define MAX_ARGS       64
#define MAX_BGPROCS    64
#define MAX_COMMANDS   16
#define MAX_CMD_TEXT   256
#define MAX_PARSE_BUF  (MAX_INPUT * 3)
#define PROMPT         "sjp-shell$ "

typedef struct {
    char *argv[MAX_ARGS];
    int   argc;
    char *input_file;
    char *output_file;
} Command;

typedef struct {
    Command commands[MAX_COMMANDS];
    int     command_count;
    int     background;
    char    parse_buffer[MAX_PARSE_BUF];
    char    command_text[MAX_CMD_TEXT];
} ParsedLine;

typedef struct {
    pid_t pids[MAX_COMMANDS];
    int   pid_count;
    int   processes_left;
    char  cmd[MAX_CMD_TEXT];
} BgProc;

BgProc bg_procs[MAX_BGPROCS];
int    bg_count = 0;

static volatile sig_atomic_t foreground_pgid = -1;

void  run_loop(FILE *input_stream, int interactive);
char *read_input(FILE *input_stream);
int   parse_input(const char *input, ParsedLine *parsed);
int   execute_line(ParsedLine *parsed);
int   execute_builtin(Command *command);
int   handle_builtin(char **args);
int   is_builtin_command(const char *name);
void  check_bg_procs(void);
void  add_bg_proc(const pid_t *pids, int pid_count, const char *cmd);
void  remove_bg_proc_at(int index);
int   find_bg_proc_by_pid(pid_t pid);
void  install_signal_handlers(void);
void  handle_shell_signal(int signal_number);
void  reset_child_signal_handlers(void);
int   apply_redirections(Command *command, int *saved_stdin, int *saved_stdout);
void  restore_redirections(int saved_stdin, int saved_stdout);
int   is_special_token(const char *token);

int main(int argc, char **argv) {
    install_signal_handlers();

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        FILE *batch_file = fopen(argv[1], "r");
        if (batch_file == NULL) {
            perror("batch file open failed");
            return EXIT_FAILURE;
        }

        run_loop(batch_file, 0);
        fclose(batch_file);
        return EXIT_SUCCESS;
    }

    run_loop(stdin, 1);
    return EXIT_SUCCESS;
}

void run_loop(FILE *input_stream, int interactive) {
    int running = 1;

    while (running) {
        char       *input;
        ParsedLine  parsed;

        check_bg_procs();

        if (interactive) {
            printf("%s", PROMPT);
            fflush(stdout);
        }

        input = read_input(input_stream);
        if (input == NULL) {
            if (interactive) {
                printf("\n");
            }
            break;
        }

        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        if (!parse_input(input, &parsed)) {
            free(input);
            continue;
        }

        running = execute_line(&parsed);
        free(input);
    }
}

char *read_input(FILE *input_stream) {
    char *buffer = malloc(MAX_INPUT);

    if (!buffer) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    if (fgets(buffer, MAX_INPUT, input_stream) == NULL) {
        free(buffer);
        return NULL;
    }

    buffer[strcspn(buffer, "\n")] = '\0';
    return buffer;
}

int parse_input(const char *input, ParsedLine *parsed) {
    char   *tokens[MAX_PARSE_BUF / 2];
    size_t  j = 0;
    int     token_count = 0;
    int     i;

    memset(parsed, 0, sizeof(*parsed));
    strncpy(parsed->command_text, input, MAX_CMD_TEXT - 1);
    parsed->command_text[MAX_CMD_TEXT - 1] = '\0';

    for (i = 0; input[i] != '\0' && j < sizeof(parsed->parse_buffer) - 2; i++) {
        if (strchr("<>|&", input[i]) != NULL) {
            parsed->parse_buffer[j++] = ' ';
            parsed->parse_buffer[j++] = input[i];
            parsed->parse_buffer[j++] = ' ';
        } else {
            parsed->parse_buffer[j++] = input[i];
        }
    }
    parsed->parse_buffer[j] = '\0';

    tokens[token_count] = strtok(parsed->parse_buffer, " \t");
    while (tokens[token_count] != NULL && token_count < (int)(sizeof(tokens) / sizeof(tokens[0])) - 1) {
        token_count++;
        tokens[token_count] = strtok(NULL, " \t");
    }

    if (token_count == 0) {
        return 0;
    }

    parsed->command_count = 1;

    for (i = 0; i < token_count; i++) {
        Command *current = &parsed->commands[parsed->command_count - 1];
        char    *token   = tokens[i];

        if (strcmp(token, "|") == 0) {
            if (current->argc == 0 || parsed->command_count >= MAX_COMMANDS) {
                fprintf(stderr, "sjp-shell: syntax error near unexpected token '|'\n");
                return 0;
            }
            current->argv[current->argc] = NULL;
            parsed->command_count++;
            continue;
        }

        if (strcmp(token, "<") == 0) {
            if (current->input_file != NULL || i + 1 >= token_count || is_special_token(tokens[i + 1])) {
                fprintf(stderr, "sjp-shell: invalid input redirection\n");
                return 0;
            }
            current->input_file = tokens[++i];
            continue;
        }

        if (strcmp(token, ">") == 0) {
            if (current->output_file != NULL || i + 1 >= token_count || is_special_token(tokens[i + 1])) {
                fprintf(stderr, "sjp-shell: invalid output redirection\n");
                return 0;
            }
            current->output_file = tokens[++i];
            continue;
        }

        if (strcmp(token, "&") == 0) {
            if (i != token_count - 1 || current->argc == 0) {
                fprintf(stderr, "sjp-shell: '&' must appear at the end of a command\n");
                return 0;
            }
            parsed->background = 1;
            continue;
        }

        if (current->argc >= MAX_ARGS - 1) {
            fprintf(stderr, "sjp-shell: too many arguments\n");
            return 0;
        }

        current->argv[current->argc++] = token;
        current->argv[current->argc] = NULL;
    }

    for (i = 0; i < parsed->command_count; i++) {
        if (parsed->commands[i].argc == 0) {
            fprintf(stderr, "sjp-shell: invalid null command\n");
            return 0;
        }
        parsed->commands[i].argv[parsed->commands[i].argc] = NULL;
    }

    return 1;
}

int execute_line(ParsedLine *parsed) {
    Command *first = &parsed->commands[0];
    int      i;

    if (parsed->command_count == 1) {
        int builtin_result;

        if (parsed->background && is_builtin_command(first->argv[0])) {
            fprintf(stderr, "sjp-shell: built-in commands cannot run in the background\n");
            return 1;
        }

        builtin_result = execute_builtin(first);
        if (builtin_result != -1) {
            return builtin_result;
        }
    }

    for (i = 0; i < parsed->command_count; i++) {
        if (is_builtin_command(parsed->commands[i].argv[0])) {
            fprintf(stderr, "sjp-shell: built-in commands cannot be used in pipelines or background jobs\n");
            return 1;
        }
    }

    {
        pid_t pids[MAX_COMMANDS];
        pid_t pgid = 0;
        int   prev_read_fd = -1;

        for (i = 0; i < parsed->command_count; i++) {
            int pipe_fds[2] = { -1, -1 };
            int is_last = (i == parsed->command_count - 1);
            pid_t pid;

            if (!is_last && pipe(pipe_fds) == -1) {
                perror("pipe failed");
                if (prev_read_fd != -1) {
                    close(prev_read_fd);
                }
                return 1;
            }

            pid = fork();
            if (pid < 0) {
                perror("fork failed");
                if (prev_read_fd != -1) {
                    close(prev_read_fd);
                }
                if (!is_last) {
                    close(pipe_fds[0]);
                    close(pipe_fds[1]);
                }
                return 1;
            }

            if (pid == 0) {
                int input_fd;
                int output_fd;

                reset_child_signal_handlers();

                if (pgid == 0) {
                    setpgid(0, 0);
                } else {
                    setpgid(0, pgid);
                }

                if (parsed->commands[i].input_file != NULL) {
                    input_fd = open(parsed->commands[i].input_file, O_RDONLY);
                    if (input_fd < 0) {
                        perror("input redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    if (dup2(input_fd, STDIN_FILENO) == -1) {
                        perror("dup2 input failed");
                        close(input_fd);
                        exit(EXIT_FAILURE);
                    }
                    close(input_fd);
                } else if (prev_read_fd != -1) {
                    if (dup2(prev_read_fd, STDIN_FILENO) == -1) {
                        perror("dup2 pipe input failed");
                        exit(EXIT_FAILURE);
                    }
                }

                if (parsed->commands[i].output_file != NULL) {
                    output_fd = open(parsed->commands[i].output_file,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (output_fd < 0) {
                        perror("output redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    if (dup2(output_fd, STDOUT_FILENO) == -1) {
                        perror("dup2 output failed");
                        close(output_fd);
                        exit(EXIT_FAILURE);
                    }
                    close(output_fd);
                } else if (!is_last) {
                    if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
                        perror("dup2 pipe output failed");
                        exit(EXIT_FAILURE);
                    }
                }

                if (prev_read_fd != -1) {
                    close(prev_read_fd);
                }
                if (!is_last) {
                    close(pipe_fds[0]);
                    close(pipe_fds[1]);
                }

                if (execvp(parsed->commands[i].argv[0], parsed->commands[i].argv) == -1) {
                    fprintf(stderr, "sjp-shell: command not found: %s\n",
                            parsed->commands[i].argv[0]);
                    exit(EXIT_FAILURE);
                }
            }

            if (pgid == 0) {
                pgid = pid;
            }
            setpgid(pid, pgid);
            pids[i] = pid;

            if (prev_read_fd != -1) {
                close(prev_read_fd);
            }
            if (!is_last) {
                close(pipe_fds[1]);
                prev_read_fd = pipe_fds[0];
            } else {
                prev_read_fd = -1;
            }
        }

        if (parsed->background) {
            printf("[%d] running in background\n", pids[0]);
            add_bg_proc(pids, parsed->command_count, parsed->command_text);
            return 1;
        }

        foreground_pgid = pgid;

        {
            int children_remaining = parsed->command_count;
            int terminated_signal  = 0;
            int stopped_signal     = 0;

            while (children_remaining > 0) {
                int   status;
                pid_t waited = waitpid(-pgid, &status, WUNTRACED);

                if (waited == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == ECHILD) {
                        break;
                    }
                    perror("waitpid failed");
                    break;
                }

                children_remaining--;

                if (WIFSTOPPED(status) && stopped_signal == 0) {
                    stopped_signal = WSTOPSIG(status);
                } else if (WIFSIGNALED(status) && terminated_signal == 0) {
                    terminated_signal = WTERMSIG(status);
                }
            }

            foreground_pgid = -1;

            if (stopped_signal != 0) {
                fprintf(stderr, "sjp-shell: process suspended by signal %d\n",
                        stopped_signal);
            } else if (terminated_signal != 0) {
                fprintf(stderr, "sjp-shell: process terminated by signal %d\n",
                        terminated_signal);
            }
        }
    }

    return 1;
}

int execute_builtin(Command *command) {
    int saved_stdin  = -1;
    int saved_stdout = -1;

    if (!is_builtin_command(command->argv[0])) {
        return -1;
    }

    if (!apply_redirections(command, &saved_stdin, &saved_stdout)) {
        restore_redirections(saved_stdin, saved_stdout);
        return 1;
    }

    {
        int builtin_result = handle_builtin(command->argv);
        restore_redirections(saved_stdin, saved_stdout);
        return builtin_result;
    }
}

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

    if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_INPUT];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd failed");
        }
        return 1;
    }

    if (strcmp(args[0], "jobs") == 0) {
        int i;

        if (bg_count == 0) {
            printf("No background processes running.\n");
        } else {
            printf("\nBackground processes:\n");
            for (i = 0; i < bg_count; i++) {
                printf("  [%d]  %s\n", bg_procs[i].pids[0], bg_procs[i].cmd);
            }
            printf("\n");
        }
        return 1;
    }

    if (strcmp(args[0], "help") == 0) {
        printf("\n=== SJP Shell - Help ===\n");
        printf("Built-in commands:\n");
        printf("  cd <dir>   - change directory\n");
        printf("  pwd        - print current directory\n");
        printf("  jobs       - list background processes\n");
        printf("  help       - show this message\n");
        printf("  exit       - exit the shell\n");
        printf("Features:\n");
        printf("  < file     - redirect input from a file\n");
        printf("  > file     - redirect output to a file\n");
        printf("  |          - pipe commands together\n");
        printf("  &          - run a command in the background\n");
        printf("  ./sjp-shell batch.txt - run commands from a file\n\n");
        return 1;
    }

    return -1;
}

int is_builtin_command(const char *name) {
    return strcmp(name, "exit") == 0 ||
           strcmp(name, "cd") == 0 ||
           strcmp(name, "pwd") == 0 ||
           strcmp(name, "jobs") == 0 ||
           strcmp(name, "help") == 0;
}

void check_bg_procs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int index = find_bg_proc_by_pid(pid);

        if (index >= 0) {
            bg_procs[index].processes_left--;
            if (bg_procs[index].processes_left <= 0) {
                printf("\n[%d] done\n", bg_procs[index].pids[0]);
                remove_bg_proc_at(index);
            }
        }
    }
}

void add_bg_proc(const pid_t *pids, int pid_count, const char *cmd) {
    int i;

    if (bg_count >= MAX_BGPROCS) {
        fprintf(stderr, "sjp-shell: too many background processes\n");
        return;
    }

    bg_procs[bg_count].pid_count = pid_count;
    bg_procs[bg_count].processes_left = pid_count;
    strncpy(bg_procs[bg_count].cmd, cmd, MAX_CMD_TEXT - 1);
    bg_procs[bg_count].cmd[MAX_CMD_TEXT - 1] = '\0';

    for (i = 0; i < pid_count; i++) {
        bg_procs[bg_count].pids[i] = pids[i];
    }

    bg_count++;
}

void remove_bg_proc_at(int index) {
    int i;

    for (i = index; i < bg_count - 1; i++) {
        bg_procs[i] = bg_procs[i + 1];
    }

    bg_count--;
}

int find_bg_proc_by_pid(pid_t pid) {
    int i;
    int j;

    for (i = 0; i < bg_count; i++) {
        for (j = 0; j < bg_procs[i].pid_count; j++) {
            if (bg_procs[i].pids[j] == pid) {
                return i;
            }
        }
    }

    return -1;
}

void install_signal_handlers(void) {
    struct sigaction action;

    action.sa_handler = handle_shell_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &action, NULL) == -1) {
        perror("sigaction SIGINT failed");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGTSTP, &action, NULL) == -1) {
        perror("sigaction SIGTSTP failed");
        exit(EXIT_FAILURE);
    }
}

void handle_shell_signal(int signal_number) {
    if (foreground_pgid > 0) {
        kill(-foreground_pgid, signal_number);
    }

    write(STDOUT_FILENO, "\n", 1);
}

void reset_child_signal_handlers(void) {
    struct sigaction action;

    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);
}

int apply_redirections(Command *command, int *saved_stdin, int *saved_stdout) {
    int input_fd;
    int output_fd;

    *saved_stdin = -1;
    *saved_stdout = -1;

    if (command->input_file != NULL) {
        input_fd = open(command->input_file, O_RDONLY);
        if (input_fd < 0) {
            perror("input redirection failed");
            return 0;
        }

        *saved_stdin = dup(STDIN_FILENO);
        if (*saved_stdin < 0 || dup2(input_fd, STDIN_FILENO) == -1) {
            perror("dup2 input failed");
            close(input_fd);
            return 0;
        }

        close(input_fd);
    }

    if (command->output_file != NULL) {
        output_fd = open(command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd < 0) {
            perror("output redirection failed");
            return 0;
        }

        *saved_stdout = dup(STDOUT_FILENO);
        if (*saved_stdout < 0 || dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 output failed");
            close(output_fd);
            return 0;
        }

        close(output_fd);
    }

    return 1;
}

void restore_redirections(int saved_stdin, int saved_stdout) {
    if (saved_stdin != -1) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    }

    if (saved_stdout != -1) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
}

int is_special_token(const char *token) {
    return strcmp(token, "<") == 0 ||
           strcmp(token, ">") == 0 ||
           strcmp(token, "|") == 0 ||
           strcmp(token, "&") == 0;
}
