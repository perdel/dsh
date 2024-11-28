#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define MAX_ARGS 1024

char *read_input(char *buffer, size_t size) {
    printf("$ ");
    return fgets(buffer, size, stdin);
}

char **parse_command(char *input, char **args, int max_args) {
    char *token = strtok(input, " \n");
    int arg_count = 0;
    while (token != NULL && arg_count < max_args) {
        args[arg_count] = token;
        token = strtok(NULL, " \n");
        arg_count++;
    }
    args[arg_count] = NULL;
    return args;
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
    int status;

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

        parse_command(command, args, MAX_ARGS);
        if (strcmp(args[0], "exit") == 0) {
            exit_command(args);
        }
        else if (strcmp(args[0], "cd") == 0) {
            change_directory(args);
        } else {
        status = execute_command(args);
        handle_exit_status(status);
        }
    }

    return 0;
}