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
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        }

        // Parent process
        wait(&status);
    }

    return 0;
}