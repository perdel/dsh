#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define MAX_ARGS 1024

int main() {
    char command[1024];
    char* args[MAX_ARGS];
    int status;

    while (1) {
        printf("$ ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
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

        // Split the command into a command and arguments
        char* token = strtok(command, " \n");
        int arg_count = 0;
        while (token != NULL && arg_count < MAX_ARGS) {
            args[arg_count] = token;
            token = strtok(NULL, " \n");
            arg_count++;
        }
        args[arg_count] = NULL;

        if (fork() == 0) {
            // Child process
            if (execvp(args[0], args) == -1) {
                fprintf(stderr, "command not found: %s\n", args[0]);
            }
            exit(EXIT_FAILURE);
        }

        int last_exit_status = 0; 

        // Parent process
        if (wait(&status) == -1) {
            perror("wait");
        };

        if (WIFEXITED(status)) {
            last_exit_status = WEXITSTATUS(status);
        }

        char exit_status_str[10];
        snprintf(exit_status_str, sizeof(exit_status_str), "%d", last_exit_status);
        setenv("?", exit_status_str, 1);
    }

    return 0;
}