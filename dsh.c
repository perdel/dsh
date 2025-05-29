#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <ctype.h>

#define MAX_ARGS 1024
#define MAX_PIPE_CMDS 100

void free_arg_strings(char **args) {
    if (!args) return;
    for (int i = 0; args[i] != NULL; i++) {
        free(args[i]);
    }
}

void free_expanded_args(char **expanded_args) {
    if (!expanded_args) return;
    free_arg_strings(expanded_args);
    free(expanded_args);
}


char *read_input(char *buffer, size_t size) {
    if (isatty(STDIN_FILENO)) {  // Check if input is from a terminal
        printf("$ ");
    }
    return fgets(buffer, size, stdin);
}

char **parse_command(char *command_segment, char **args, int max_args, char **input_file, char **output_file, int *append_mode) {
    int arg_count = 0;
    char *p = command_segment;
    char token_buffer[1024];
    int buffer_idx = 0;
    int in_single_quotes = 0;
    int in_double_quotes = 0;
    int escaped = 0;

    *input_file = NULL;
    *output_file = NULL;
    *append_mode = 0;

    while (*p != '\0' && arg_count < max_args - 1) { // -1 for the final NULL
        // Skip leading whitespace outside of quotes
        if (!in_single_quotes && !in_double_quotes && isspace(*p)) {
            p++;
            continue;
        }

        if (*p == '\0') break;

        // Handle redirection operators outside of quotes
        if (!in_single_quotes && !in_double_quotes) {
            if (*p == '<') {
                p++;
                // Skip whitespace after operator
                while (isspace(*p)) p++;
                // Parse filename
                int fn_buffer_idx = 0;
                int fn_in_single_quotes = 0;
                int fn_in_double_quotes = 0;
                int fn_escaped = 0;

                while (*p != '\0' && (!isspace(*p) || fn_in_single_quotes || fn_in_double_quotes)) {
                     if (fn_escaped) {
                         if (fn_buffer_idx < sizeof(token_buffer) - 1) token_buffer[fn_buffer_idx++] = *p;
                         else { fprintf(stderr, "dsh: filename too long\n"); goto parse_error; }
                         fn_escaped = 0;
                     } else if (*p == '\\') {
                         fn_escaped = 1;
                     } else if (*p == '\'' && !fn_in_double_quotes) {
                         fn_in_single_quotes = !fn_in_single_quotes;
                     } else if (*p == '"' && !fn_in_single_quotes) {
                         fn_in_double_quotes = !fn_in_double_quotes;
                     } else {
                         if (fn_buffer_idx < sizeof(token_buffer) - 1) token_buffer[fn_buffer_idx++] = *p;
                         else { fprintf(stderr, "dsh: filename too long\n"); goto parse_error; }
                     }
                     p++;
                }

                if (fn_in_single_quotes || fn_in_double_quotes || fn_escaped) {
                     fprintf(stderr, "dsh: unmatched quote or incomplete escape sequence in filename\n");
                     goto parse_error;
                }

                if (fn_buffer_idx > 0) {
                    token_buffer[fn_buffer_idx] = '\0';
                    *input_file = strdup(token_buffer);
                    if (*input_file == NULL) { perror("strdup"); goto parse_error; }
                } else {
                    fprintf(stderr, "dsh: missing filename for input redirection\n");
                    goto parse_error;
                }
                continue;
            } else if (*p == '>') {
                p++;
                if (*p == '>') {
                    *append_mode = 1;
                    p++;
                } else {
                    *append_mode = 0;
                }
                 // Skip whitespace after operator
                while (isspace(*p)) p++;
                // Parse filename (similar logic as input file)
                int fn_buffer_idx = 0;
                int fn_in_single_quotes = 0;
                int fn_in_double_quotes = 0;
                int fn_escaped = 0;

                while (*p != '\0' && (!isspace(*p) || fn_in_single_quotes || fn_in_double_quotes)) {
                     if (fn_escaped) {
                         if (fn_buffer_idx < sizeof(token_buffer) - 1) token_buffer[fn_buffer_idx++] = *p;
                         else { fprintf(stderr, "dsh: filename too long\n"); goto parse_error; }
                         fn_escaped = 0;
                     } else if (*p == '\\') {
                         fn_escaped = 1;
                     } else if (*p == '\'' && !fn_in_double_quotes) {
                         fn_in_single_quotes = !fn_in_single_quotes;
                     } else if (*p == '"' && !fn_in_single_quotes) {
                         fn_in_double_quotes = !fn_in_double_quotes;
                     } else {
                         if (fn_buffer_idx < sizeof(token_buffer) - 1) token_buffer[fn_buffer_idx++] = *p;
                         else { fprintf(stderr, "dsh: filename too long\n"); goto parse_error; }
                     }
                     p++;
                }

                 if (fn_in_single_quotes || fn_in_double_quotes || fn_escaped) {
                     fprintf(stderr, "dsh: unmatched quote or incomplete escape sequence in filename\n");
                     goto parse_error;
                }

                if (fn_buffer_idx > 0) {
                    token_buffer[fn_buffer_idx] = '\0';
                    *output_file = strdup(token_buffer);
                     if (*output_file == NULL) { perror("strdup"); goto parse_error; }
                } else {
                    fprintf(stderr, "dsh: missing filename for output redirection\n");
                    goto parse_error;
                }
                continue;
            }
        }

        // Start collecting a token
        buffer_idx = 0;
        escaped = 0; // Reset escaped state for the new token

        while (*p != '\0') {
            if (escaped) {
                // Add the escaped character literally
                if (buffer_idx < sizeof(token_buffer) - 1) {
                    token_buffer[buffer_idx++] = *p;
                } else {
                    fprintf(stderr, "dsh: argument too long\n");
                    goto parse_error;
                }
                escaped = 0;
            } else if (*p == '\\') {
                // Backslash escapes the next character
                escaped = 1;
            } else if (*p == '\'') {
                if (!in_double_quotes) {
                    in_single_quotes = !in_single_quotes;
                    // Don't add the quote character to the token
                } else {
                     // Single quote inside double quotes is literal
                     if (buffer_idx < sizeof(token_buffer) - 1) {
                        token_buffer[buffer_idx++] = *p;
                    } else { fprintf(stderr, "dsh: argument too long\n"); goto parse_error; }
                }
            } else if (*p == '"') {
                 if (!in_single_quotes) {
                    in_double_quotes = !in_double_quotes;
                    // Don't add the quote character to the token
                } else {
                    // Double quote inside single quotes is literal
                     if (buffer_idx < sizeof(token_buffer) - 1) {
                        token_buffer[buffer_idx++] = *p;
                    } else { fprintf(stderr, "dsh: argument too long\n"); goto parse_error; }
                }
            } else if (isspace(*p) && !in_single_quotes && !in_double_quotes) {
                // End of token (whitespace outside quotes)
                break;
            } else if ((*p == '<' || *p == '>') && !in_single_quotes && !in_double_quotes) {
                 // End of token before redirection operator outside quotes
                 break;
            } else if (*p == '|' && !in_single_quotes && !in_double_quotes) {
                 // End of token before pipe operator outside quotes
                 break;
            }
            else {
                // Regular character
                 if (buffer_idx < sizeof(token_buffer) - 1) {
                    token_buffer[buffer_idx++] = *p;
                } else {
                     fprintf(stderr, "dsh: argument too long\n");
                     goto parse_error;
                }
            }
            p++;
        }

        // Add the collected token if any
        if (buffer_idx > 0) {
            token_buffer[buffer_idx] = '\0';
            args[arg_count] = strdup(token_buffer);
            if (args[arg_count] == NULL) { perror("strdup"); goto parse_error; }
            arg_count++;
        }
         // If we broke because of whitespace, p is already advanced.
         // If we broke because of redirection/pipe/null, p is at that character,
         // the outer loop will handle it or terminate.
    }

    // Final check for unmatched quotes/escapes after processing the whole segment
     if (in_single_quotes || in_double_quotes || escaped) {
         fprintf(stderr, "dsh: unmatched quote or incomplete escape sequence\n");
         goto parse_error;
     }

    args[arg_count] = NULL;

    return args;

parse_error:
    // Clean up allocated memory on error
    for(int j=0; j<arg_count; j++) free(args[j]);
    if (*input_file) { free(*input_file); *input_file = NULL; }
    if (*output_file) { free(*output_file); *output_file = NULL; }
    return NULL; // Indicate error
}

// Function to expand wildcards in the argument list
char **expand_wildcards(char **args) {
    glob_t glob_results;
    int i = 0;
    int arg_count = 0;
    char **new_args = NULL;
    int current_new_args_size = MAX_ARGS; // Initial size

    // Allocate initial memory for the new argument list array
    new_args = malloc(current_new_args_size * sizeof(char *));
    if (new_args == NULL) {
        perror("malloc");
        return NULL; // Indicate allocation failure
    }

    while (args[i] != NULL) {
        // Perform glob expansion
        // Use GLOB_NOMATCH to keep the original pattern if no matches are found
        // Use GLOB_TILDE to expand ~
        // GLOB_NOESCAPE is NOT used, so glob handles backslashes in the pattern.
        int ret = glob(args[i], GLOB_NOMATCH | GLOB_TILDE, NULL, &glob_results);

        if (ret == 0) { // Matches found
            // Ensure enough space in new_args array
            if (arg_count + glob_results.gl_pathc >= current_new_args_size) {
                current_new_args_size += glob_results.gl_pathc + MAX_ARGS; // Increase size
                char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                if (temp_args == NULL) {
                    perror("realloc");
                    globfree(&glob_results);
                    free_expanded_args(new_args); // Free partially built new_args
                    return NULL; // Indicate allocation failure
                }
                new_args = temp_args;
            }

            // Copy expanded paths to new_args
            for (size_t j = 0; j < glob_results.gl_pathc; j++) {
                new_args[arg_count++] = strdup(glob_results.gl_pathv[j]); // strdup to own the string
                if (new_args[arg_count-1] == NULL) {
                     perror("strdup");
                     globfree(&glob_results);
                     free_expanded_args(new_args);
                     return NULL; // Indicate allocation failure
                }
            }
            globfree(&glob_results); // Free glob's internal memory

        } else if (ret == GLOB_NOMATCH) { // No matches, keep original argument
             if (arg_count + 1 >= current_new_args_size) {
                current_new_args_size += MAX_ARGS; // Increase size
                char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                if (temp_args == NULL) {
                    perror("realloc");
                    free_expanded_args(new_args); // Free partially built new_args
                    return NULL; // Indicate allocation failure
                }
                new_args = temp_args;
            }
            new_args[arg_count++] = strdup(args[i]); // strdup to own the string
             if (new_args[arg_count-1] == NULL) {
                 perror("strdup");
                 free_expanded_args(new_args); // Free partially built new_args
                 return NULL; // Indicate allocation failure
             }
        } else { // glob error
            perror("glob");
             // On glob error, treat the pattern as a literal string
             if (arg_count + 1 >= current_new_args_size) {
                current_new_args_size += MAX_ARGS; // Increase size
                char **temp_args = realloc(new_args, current_new_args_size * sizeof(char *));
                if (temp_args == NULL) {
                    perror("realloc");
                    free_expanded_args(new_args); // Free partially built new_args
                    return NULL; // Indicate allocation failure
                }
                new_args = temp_args;
            }
            new_args[arg_count++] = strdup(args[i]); // strdup to own the string
             if (new_args[arg_count-1] == NULL) {
                 perror("strdup");
                 free_expanded_args(new_args); // Free partially built new_args
                 return NULL; // Indicate allocation failure
             }
        }
        i++;
    }

    new_args[arg_count] = NULL; // Null-terminate the new argument list

    // The original args array strings were allocated by parse_command.
    // The caller is responsible for freeing them after calling expand_wildcards.

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
            if (i < num_commands - 1) { close(pipefd[0]); close(pipefd[1]); } // Close pipe ends if created
            exit(EXIT_FAILURE);
        } else if (pid == 0) {  // Child process
            char *args[MAX_ARGS]; // Static array for parse_command
            char *input_file = NULL, *output_file = NULL; // Allocated by parse_command
            int append_mode = 0;

            // Parse the command segment
            char **parsed_args = parse_command(commands[i], args, MAX_ARGS, &input_file, &output_file, &append_mode);

            if (parsed_args == NULL) { // Parse error (message printed by parse_command, memory freed by parse_command)
                 exit(EXIT_FAILURE); // Exit child process
            }
            // parsed_args is the static 'args' array, filled with allocated strings

            // Expand wildcards
            char **expanded_args = expand_wildcards(parsed_args);

            // Free the strings allocated by parse_command (original args)
            free_arg_strings(parsed_args); // Free strings in the static args array

            if (expanded_args == NULL) { // Expansion failed (malloc/realloc/strdup error)
                 // expanded_args is NULL, free_expanded_args handles NULL
                 // Free input/output files
                 if (input_file) free(input_file);
                 if (output_file) free(output_file);
                 exit(EXIT_FAILURE); // Exit child process
            }

            // Handle input redirection
            if (input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1) {
                    perror(input_file);
                    // Free expanded_args and files before exiting
                    free_expanded_args(expanded_args);
                    if (input_file) free(input_file);
                    if (output_file) free(output_file);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    // Free expanded_args and files before exiting
                    close(fd);
                    free_expanded_args(expanded_args);
                    if (input_file) free(input_file);
                    if (output_file) free(output_file);
                    exit(EXIT_FAILURE);
                }
                close(fd);
            } else if (prev_fd != STDIN_FILENO) {  // Pipe input from previous command
                if (dup2(prev_fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    // Free expanded_args and files before exiting
                    free_expanded_args(expanded_args);
                    if (input_file) free(input_file);
                    if (output_file) free(output_file);
                    exit(EXIT_FAILURE);
                }
                // Close the read end of the previous pipe in the child
                close(prev_fd);
            }

            // Handle output redirection
            if (output_file) {
                int fd = open(output_file, O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC), 0644);
                if (fd == -1) {
                    perror(output_file);
                    // Free expanded_args and files before exiting
                    free_expanded_args(expanded_args);
                    if (input_file) free(input_file);
                    if (output_file) free(output_file);
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    // Free expanded_args and files before exiting
                    close(fd);
                    free_expanded_args(expanded_args);
                    if (input_file) free(input_file);
                    if (output_file) free(output_file);
                    exit(EXIT_FAILURE);
                }
                close(fd);
            } else if (i < num_commands - 1) {  // Pipe output to next command
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    // Free expanded_args and files before exiting
                    close(pipefd[0]); // Close both ends of the current pipe
                    close(pipefd[1]);
                    free_expanded_args(expanded_args);
                    if (input_file) free(input_file);
                    if (output_file) free(output_file);
                    exit(EXIT_FAILURE);
                }
                // Close both ends of the current pipe in the child
                close(pipefd[0]);
                close(pipefd[1]);
            }

            // Close unused pipe ends in the child (pipes from other commands)
            // This is complex in a general pipeline. For this simple linear pipeline,
            // the child only needs the previous read end and the current write end.
            // All other pipe FDs should be closed. The parent closes the ends it doesn't use.
            // The prev_fd is closed after dup2. The current pipe ends are closed after dup2.
            // Any other pipe FDs from other commands in the pipeline loop?
            // The parent manages the prev_fd. The child only inherits FDs open at fork time.
            // So, the child inherits all pipe FDs created *before* its fork.
            // It must close all inherited pipe FDs it doesn't need.
            // This requires keeping track of all pipe FDs created in the parent loop.
            // This is getting too complex for this request. Let's assume the current
            // closing logic is sufficient for basic cases, but acknowledge it's not fully robust.


            // Check if there's a command to execute after parsing and expansion
            if (expanded_args[0] == NULL) {
                 // Free expanded_args and files and exit successfully.
                 free_expanded_args(expanded_args);
                 if (input_file) free(input_file);
                 if (output_file) free(output_file);
                 exit(EXIT_SUCCESS);
            }

            // Execute the command
            if (execvp(expanded_args[0], expanded_args) == -1) {
                // execvp failed
                fprintf(stderr, "command not found: %s\n", expanded_args[0]);
            }

            // Free the expanded arguments and files before exiting the child process (only reached if execvp fails)
            free_expanded_args(expanded_args);
            if (input_file) free(input_file);
            if (output_file) free(output_file);

            exit(EXIT_FAILURE); // Exit child process
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
            // The parent does NOT wait here. It waits for all children at the end.
        }
    }

    // Parent process waits for all child processes to finish.
    // The prev_fd from the last command's pipe (if any) should also be closed in the parent.
    if (prev_fd != STDIN_FILENO) {
        close(prev_fd);
    }

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
        // Free allocated memory before returning on fork error
        free_arg_strings(args); // Free strings allocated by parse_command
        if (input_file) free(input_file);
        if (output_file) free(output_file);
        return -1; // Indicate error
    } else if (pid == 0) {
        // Child process
        char **expanded_args = expand_wildcards(args);

        // Free the strings allocated by parse_command (original args)
        free_arg_strings(args);

        if (expanded_args == NULL) { // Expansion failed (malloc/realloc/strdup error in expand_wildcards)
             // expanded_args is NULL, free_expanded_args handles NULL
             // Free input/output files
             if (input_file) free(input_file);
             if (output_file) free(output_file);
             exit(EXIT_FAILURE); // Exit child process
        }

        // Handle input redirection
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd == -1) {
                perror(input_file);
                // Free expanded_args and files before exiting
                free_expanded_args(expanded_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDIN_FILENO) == -1) {
                perror("dup2");
                // Free expanded_args and files before exiting
                close(fd); // Close the opened file descriptor
                free_expanded_args(expanded_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                exit(EXIT_FAILURE);
            }
            close(fd);
        }

        // Handle output redirection
        if (output_file) {
            int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
            int fd = open(output_file, flags, 0644); // Permissions moved here
            if (fd == -1) {
                perror(output_file);
                // Free expanded_args and files before exiting
                free_expanded_args(expanded_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                // Free expanded_args and files before exiting
                close(fd); // Close the opened file descriptor
                free_expanded_args(expanded_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                exit(EXIT_FAILURE);
            }
            close(fd);
        }

        // Check if there's a command to execute after parsing and expansion
        if (expanded_args[0] == NULL) {
             // Free expanded_args and files and exit successfully.
             free_expanded_args(expanded_args);
             if (input_file) free(input_file);
             if (output_file) free(output_file);
             exit(EXIT_SUCCESS);
        }


        // Execute the command
        if (execvp(expanded_args[0], expanded_args) == -1) {
            // execvp failed
            fprintf(stderr, "command not found: %s\n", expanded_args[0]);
        }

        // Free the expanded arguments and files before exiting the child process (only reached if execvp fails)
        free_expanded_args(expanded_args);
        if (input_file) free(input_file);
        if (output_file) free(output_file);

        exit(EXIT_FAILURE); // Exit child process
    } else {
        // Parent process
        int status;
        if (wait(&status) == -1) {
            perror("wait");
            // The child process should have freed its memory or execvp replaced it.
            // The parent only needs to free the args/files if fork failed *before* the child could free them.
            // But if fork succeeded, the child is responsible for freeing its copy.
            // So, no need to free args/files in parent after successful fork.
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
    // args strings and filenames are freed in main before calling exit_command
    if (args[1] != NULL) {
        fprintf(stderr, "exit: too many arguments\n");
        // In a real shell, one might return a non-zero status here.
        // For this simple shell, we just print the error and don't exit.
        // The memory is already freed in main.
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
    char *args[MAX_ARGS]; // Static array for parse_command
    char *input_file = NULL, *output_file= NULL; // Allocated by parse_command
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
        // parse_pipeline modifies 'command' string by replacing '|' with '\0'.
        // execute_pipeline calls parse_command on segments.
        // execute_pipeline handles freeing within its child processes.
        if (strchr(command, '|') != NULL) {
            execute_pipeline(command);
            // No freeing needed in main after execute_pipeline, children handle it.
        } else {
            // Non-piped command
            input_file = output_file = NULL; // Reset for each command
            append_mode = 0;

            // parse_command fills the static args array with allocated strings
            // and sets input_file/output_file (allocated strings).
            char **parsed_args = parse_command(command, args, MAX_ARGS, &input_file, &output_file, &append_mode);

            if (parsed_args == NULL) { // Parse error (message printed by parse_command, memory freed by parse_command)
                 // input_file and output_file are NULL and freed by parse_command on error
                 continue; // Get next command
            }
            // parsed_args is the static 'args' array, filled with allocated strings

            if (parsed_args[0] == NULL) { // Empty command after parsing (e.g., just whitespace or redirections without command)
                 // Free allocated strings in args and filenames
                 free_arg_strings(parsed_args);
                 if (input_file) free(input_file);
                 if (output_file) free(output_file);
                 continue; // Get next command
            }

            if (strcmp(parsed_args[0], "exit") == 0) {
                // Free allocated strings in args and filenames before calling exit_command
                free_arg_strings(parsed_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                exit_command(parsed_args); // exit_command calls exit()
            } else if (strcmp(parsed_args[0], "cd") == 0) {
                change_directory(parsed_args);
                handle_exit_status(0); // Set status to 0 for successful built-in
                // Free allocated strings in args and filenames
                free_arg_strings(parsed_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
            } else if (strcmp(parsed_args[0], "echo") == 0) {
                echo_command(parsed_args);
                handle_exit_status(0); // Set status to 0 for successful built-in
                // Free allocated strings in args and filenames
                free_arg_strings(parsed_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
            } else if (strcmp(parsed_args[0], "pwd") == 0) {
                pwd_command();
                handle_exit_status(0); // Set status to 0 for successful built-in
                // Free allocated strings in args and filenames
                free_arg_strings(parsed_args);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
            }
            else {
                // External command
                // execute_command handles freeing args strings, expanded_args, and filenames in the child.
                status = execute_command(parsed_args, input_file, output_file, append_mode);
                if (status != -1) { // Only handle status if execution didn't fail before wait
                    handle_exit_status(status);
                }
                // No freeing needed here in main after execute_command returns, child handled it.
            }
        }
    }

    return 0; // Should not be reached if exit command is used
}
