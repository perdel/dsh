#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>

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

// Function to expand wildcards in the argument list
char **expand_wildcards(char **args) {
    glob_t glob_results;
    int i = 0;
    int arg_count = 0;
    char **new_args = NULL;
    int current_new_args_size = MAX_ARGS; // Initial size, will realloc if needed

    // Allocate initial memory for the new argument list
    new_args = malloc(current_new_args_size * sizeof(char *));
    if (new_args == NULL) {
        perror("malloc");
        return args; // Return original args on failure
    }

    while (args[i] != NULL) {
        // Check if the argument contains wildcard characters
        if (strchr(args[i], '*') != NULL || strchr(args[i], '?') != NULL || strchr(args[i], '[') != NULL) {
            // Perform glob expansion
            // Use GLOB_NOMATCH to keep the original pattern if no matches are found
            int ret = glob(args[i], GLOB_NOMATCH | GLOB_TILDE, NULL, &glob_results);

            if (ret == 0) { // Matches found
                // Ensure enough space in new_args
                if (arg_count + glob_results.gl_pathc >= current_new_args_size) {
                    current_new_args_size += glob_results.gl_pathc + MAX_ARGS; // Increase size
                    char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                    if (temp_args == NULL) {
                        perror("realloc");
                        globfree(&glob_results);
                        // Free partially built new_args if realloc fails
                        // Note: This is tricky; for simplicity, we might just return original args
                        // and leak the partial new_args here in a real shell you'd handle this better.
                        free(new_args);
                        return args;
                    }
                    new_args = temp_args;
                }

                // Copy expanded paths to new_args
                for (size_t j = 0; j < glob_results.gl_pathc; j++) {
                    new_args[arg_count++] = strdup(glob_results.gl_pathv[j]); // strdup to own the string
                }
                globfree(&glob_results); // Free glob's internal memory

            } else if (ret == GLOB_NOMATCH) { // No matches, keep original argument
                 if (arg_count + 1 >= current_new_args_size) {
                    current_new_args_size += MAX_ARGS; // Increase size
                    char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                    if (temp_args == NULL) {
                        perror("realloc");
                        // Free partially built new_args
                        free(new_args);
                        return args;
                    }
                    new_args = temp_args;
                }
                new_args[arg_count++] = strdup(args[i]); // strdup to own the string
            } else { // glob error
                perror("glob");
                 if (arg_count + 1 >= current_new_args_size) {
                    current_new_args_size += MAX_ARGS; // Increase size
                    char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                    if (temp_args == NULL) {
                        perror("realloc");
                        // Free partially built new_args
                        free(new_args);
                        return args;
                    }
                    new_args = temp_args;
                }
                new_args[arg_count++] = strdup(args[i]); // strdup to own the string
            }
        } else { // No wildcards, keep original argument
             if (arg_count + 1 >= current_new_args_size) {
                current_new_args_size += MAX_ARGS; // Increase size
                char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                if (temp_args == NULL) {
                    perror("realloc");
                    // Free partially built new_args
                    free(new_args);
                    return args;
                }
                new_args = temp_args;
            }
            new_args[arg_count++] = strdup(args[i]); // strdup to own the string
        }
        i++;
    }

    new_args[arg_count] = NULL; // Null-terminate the new argument list

    // Free the original arguments if they were dynamically allocated (not the case here, but good practice)
    // For this shell, args is a fixed-size array, so no need to free args itself.
    // However, the strings within args are from the input buffer, which is freed later.

    return new_args;
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

            char **expanded_args = expand_wildcards(args);
            if (expanded_args == args) { // Expansion failed or no wildcards, use original
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
            } else { // Expansion successful, use expanded_args
                 // Handle input redirection
                if (input_file) {
                    int fd = open(input_file, O_RDONLY);
                    if (fd == -1) {
                        perror(input_file);
                        // Free expanded_args before exiting
                        for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                        free(expanded_args);
                        exit(EXIT_FAILURE);
                    }
                    if (dup2(fd, STDIN_FILENO) == -1) {
                        perror("dup2");
                        // Free expanded_args before exiting
                        for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                        free(expanded_args);
                        exit(EXIT_FAILURE);
                    }
                    close(fd);
                } else if (prev_fd != STDIN_FILENO) {  // Pipe input
                    if (dup2(prev_fd, STDIN_FILENO) == -1) {
                        perror("dup2");
                        // Free expanded_args before exiting
                        for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                        free(expanded_args);
                        exit(EXIT_FAILURE);
                    }
                    close(prev_fd);
                }

                // Handle output redirection
                if (output_file) {
                    int fd = open(output_file, O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC), 0644);
                    if (fd == -1) {
                        perror(output_file);
                        // Free expanded_args before exiting
                        for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                        free(expanded_args);
                        exit(EXIT_FAILURE);
                    }
                    if (dup2(fd, STDOUT_FILENO) == -1) {
                        perror("dup2");
                        // Free expanded_args before exiting
                        for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                        free(expanded_args);
                        exit(EXIT_FAILURE);
                    }
                    close(fd);
                } else if (i < num_commands - 1) {  // Pipe output
                    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                        perror("dup2");
                        // Free expanded_args before exiting
                        for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                        free(expanded_args);
                        exit(EXIT_FAILURE);
                    }
                    close(pipefd[0]);
                    close(pipefd[1]);
                }

                if (execvp(expanded_args[0], expanded_args) == -1) {
                    perror(expanded_args[0]);
                }

                // Free the expanded arguments before exiting the child process
                for(int j=0; expanded_args[j] != NULL; j++) free(expanded_args[j]);
                free(expanded_args);

                exit(EXIT_FAILURE); // Exit child process
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

int execute_command(char **args, char *input_file, char *output_file, int append_mode) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1; // Indicate error
    } else if (pid == 0) {
        // Child process
        char **expanded_args = expand_wildcards(args);
        if (expanded_args == args) { // Expansion failed or no wildcards, use original
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
            }

            // Handle output redirection
            if (output_file) {
                int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
                int fd = open(output_file, flags, 0644);
                if (fd == -1) {
                    perror(output_file);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            if (execvp(args[0], args) == -1) {
                fprintf(stderr, "command not found: %s\n", args[0]);
            }
            exit(EXIT_FAILURE); // Exit child process
        } else { // Expansion successful, use expanded_args
            // Handle input redirection
            if (input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1) {
                    perror(input_file);
                    // Free expanded_args before exiting
                    for(int i=0; expanded_args[i] != NULL; i++) free(expanded_args[i]);
                    free(expanded_args);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                     // Free expanded_args before exiting
                    for(int i=0; expanded_args[i] != NULL; i++) free(expanded_args[i]);
                    free(expanded_args);
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            // Handle output redirection
            if (output_file) {
                int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
                int fd = open(output_file, flags, 0644);
                if (fd == -1) {
                    perror(output_file);
                     // Free expanded_args before exiting
                    for(int i=0; expanded_args[i] != NULL; i++) free(expanded_args[i]);
                    free(expanded_args);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                     // Free expanded_args before exiting
                    for(int i=0; expanded_args[i] != NULL; i++) free(expanded_args[i]);
                    free(expanded_args);
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            if (execvp(expanded_args[0], expanded_args) == -1) {
                fprintf(stderr, "command not found: %s\n", expanded_args[0]);
            }

            // Free the expanded arguments before exiting the child process
            for(int i=0; expanded_args[i] != NULL; i++) free(expanded_args[i]);
            free(expanded_args);

            exit(EXIT_FAILURE); // Exit child process
        }
    } else {
        // Parent process
        int status;
        if (wait(&status) == -1) {
            perror("wait");
            return -1; // Indicate error
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

void echo_command(char **args) {
    // Start from the second argument (args[1])
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s%s", args[i], args[i+1] != NULL ? " " : "");
    }
    printf("\n");
}

void pwd_command() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("getcwd");
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
                handle_exit_status(0); // Set status to 0 for successful built-in
            } else if (strcmp(args[0], "echo") == 0) {
                echo_command(args);
                handle_exit_status(0); // Set status to 0 for successful built-in
            } else if (strcmp(args[0], "pwd") == 0) {
                pwd_command();
                handle_exit_status(0); // Set status to 0 for successful built-in
            }
            else {
                status = execute_command(args, input_file, output_file, append_mode);
                if (status != -1) { // Only handle status if execution didn't fail before wait
                    handle_exit_status(status);
                }
            }
        }
    }

    return 0;
}
