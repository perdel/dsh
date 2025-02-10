#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_ARGS 1024
#define MAX_PIPE_CMDS 100

char *read_input(char *buffer, size_t size) {
    if (isatty(STDIN_FILENO)) {  // Check if input is from a terminal
        printf("$ ");
    }
    return fgets(buffer, size, stdin);
}

char **parse_command(char *input, char **args, int max_args, char **input_file, char **output_file, int *append_mode) {
    char *token = strtok(input, " \n");
    int arg_count = 0;
    
    while (token != NULL && arg_count < max_args) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \n");
            if (token) *input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \n");
            if (token) *output_file = token;
            *append_mode = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \n");
            if (token) *output_file = token;
            *append_mode = 1;
        } else {
            args[arg_count++] = token;
        }
        token = strtok(NULL, " \n");
    }
    args[arg_count] = NULL;
    return args;
}

void parse_pipeline(char *input, char **commands, int *num_commands) {
    *num_commands = 0;
    char *command = strtok(input, "|");
    while (command != NULL && *num_commands < MAX_PIPE_CMDS) {
        // Remove leading whitespace from each command
        while (*command == ' ') command++;
        commands[(*num_commands)++] = command;
        command = strtok(NULL, "|");
    }
    commands[*num_commands] = NULL;
}

void execute_pipeline(char *input_line) {
    char *commands[MAX_PIPE_CMDS];
    int num_commands = 0;
    parse_pipeline(input_line, commands, &num_commands);

    int prev_fd = STDIN_FILENO;  // Input for first command
    char *input_file = NULL, *output_file = NULL;
    int append_mode = 0;

    for (int i = 0; i < num_commands; i++) {
        int pipefd[2] = {-1, -1};

        // For every command except the last, create a new pipe.
        if (i < num_commands - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {  // Child process
            char *args[MAX_ARGS];
            input_file = output_file = NULL;
            append_mode = 0;
            parse_command(commands[i], args, MAX_ARGS, &input_file, &output_file, &append_mode);

            // Handle input redirection
            if (input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1) {
                    perror(input_file);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            } else if (prev_fd != STDIN_FILENO) {  // Pipe input
                if (dup2(prev_fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(prev_fd);
            }

            // Handle output redirection
            if (output_file) {
                int fd = open(output_file, O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC), 0644);
                if (fd == -1) {
                    perror(output_file);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            } else if (i < num_commands - 1) {  // Pipe output
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(pipefd[0]);
                close(pipefd[1]);
            }

            if (execvp(args[0], args) == -1) {
                perror(args[0]);
                exit(EXIT_FAILURE);
            }
        } else {  // Parent process
            // Close previous input file descriptor if not STDIN.
            if (prev_fd != STDIN_FILENO) {
                close(prev_fd);
            }
            // Close the write end of the current pipe.
            if (i < num_commands - 1) {
                close(pipefd[1]);
                prev_fd = pipefd[0];  // The read end becomes input for the next command.
            }
        }
    }

    // Wait for all child processes to finish.
    for (int i = 0; i < num_commands; i++) {
        if (wait(NULL) == -1) {
            perror("wait");
        }
    }
}

int execute_command(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "command not found: %s\n", args[0]);
        }
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        int status;
        if (wait(&status) == -1) {
            perror("wait");
        }
        return WEXITSTATUS(status);
    }
}

void handle_exit_status(int status) {
    char exit_status_str[10];
    snprintf(exit_status_str, sizeof(exit_status_str), "%d", status);
    setenv("?", exit_status_str, 1);
}

void exit_command(char **args) {
    if (args[1] != NULL) {
        fprintf(stderr, "exit: too many arguments\n");
        return;
    }

    printf("Goodbye!\n");
    exit(EXIT_SUCCESS);
}

void change_directory(char **args) {
    if (args[1] == NULL) {
        chdir(getenv("HOME"));
    } else {
        if (strcmp(args[1], "..") == 0) {
            chdir("..");
        } else if (strcmp(args[1], ".") == 0) {
            return;
        } else if (strcmp(args[1], "~") == 0) {
            chdir(getenv("HOME"));
        } else if (strcmp(args[1], "-") == 0) {
           if (chdir(getenv("OLDPWD")) == -1) {
                fprintf(stderr, "cd: OLDPWD not set\n"); 
           }
        } else {
            if(chdir(args[1]) == -1) {
                fprintf(stderr, "cd: %s: No such file or directory\n", args[1]);
            };
        }
    }
}

int main() {
    char command[1024];
    char *args[MAX_ARGS];
    char *input_file = NULL, *output_file= NULL;
    int status;
    int append_mode = 0;

    while (1) {
        if (read_input(command, sizeof(command)) == NULL) {
            if (feof(stdin)) {
                // Exit shell when CTL+D is pressed
                printf("\n");
                break;
            } else {
            // Some other error occurred
                perror("fgets");
                break;
            }
        }
        // If the command contains a pipe, use the pipeline execution.
        if (strchr(command, '|') != NULL) {
            execute_pipeline(command);
        } else {
            input_file = output_file = NULL;
            append_mode = 0;
            parse_command(command, args, MAX_ARGS, &input_file, &output_file, &append_mode);

            if (args[0] == NULL) continue;  // Empty command

            if (strcmp(args[0], "exit") == 0) {
                exit_command(args);
            } else if (strcmp(args[0], "cd") == 0) {
                change_directory(args);
            } else {
                status = execute_command(args);
                handle_exit_status(status);
            }
        }
    }

    return 0;
}