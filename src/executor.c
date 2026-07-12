#include "my_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// Executes a command by forking and running it in a child process.
int executor(char** args, char** env)
{
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) { // child process
        if (child_process(args, env) == 0) {
            _exit(0);
        }
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    if (WIFSIGNALED(status) && WTERMSIG(status) != SIGINT) {
        printf("Process terminated by signal: %d\n", WTERMSIG(status));
    }
    return 1;
}

// Resolve an external command to a full path: use it literally if it contains
// a '/', otherwise search PATH. Returns a malloc'd path or NULL if not found.
char* resolve_exec(const char* cmd, char** env)
{
    if (my_strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) {
            return my_strdup(cmd);
        }
        return NULL;
    }

    char* path_string = get_path(env);
    if (!path_string) {
        return NULL;
    }

    int num_paths;
    char** path_list = split_paths(path_string, &num_paths);
    char* found = NULL;
    for (int i = 0; i < num_paths; i++) {
        char full_path[MAX_INPUT];
        snprintf(full_path, sizeof(full_path), "%s/%s", path_list[i], cmd);
        if (access(full_path, X_OK) == 0) {
            found = my_strdup(full_path);
            break;
        }
    }

    for (int i = 0; i < num_paths; i++) free(path_list[i]);
    free(path_string);
    free(path_list);
    return found;
}

// Attempts to execute the command by searching paths / the current directory.
int child_process(char** args, char** env)
{
    char* path = resolve_exec(args[0], env);
    if (!path) {
        fprintf(stderr, "my_shell: command not found: %s\n", args[0]);
        return 127;
    }
    execve(path, args, env);
    perror("execve");
    free(path);
    return 127;
}

// Fetches the PATH environment variable.
char* get_path(char** env) {
    for (int i = 0; env[i]; i++) {
        if (my_strncmp(env[i], "PATH=", 5) == 0) {
            return my_strdup(env[i] + 5);
        }
    }
    return NULL;
}

// Split the PATH string into individual paths (NULL-terminated array).
char** split_paths(char* paths, int* count) {
    char** result = NULL;
    size_t size_of_path = my_strlen(paths);
    char paths_copy[size_of_path + 1];

    my_strncpy(paths_copy, paths, sizeof(paths_copy));
    paths_copy[sizeof(paths_copy) - 1] = '\0';

    char* token = my_strtok(paths_copy, ":");
    *count = 0;

    while (token) {
        result = realloc(result, ((*count + 1) * sizeof(char*)));
        if (!result) {
            perror("realloc");
            return NULL;
        }
        result[*count] = my_strdup(token);
        if (!result[*count]) {
            perror("my_strdup");
            return NULL;
        }
        (*count)++;
        token = my_strtok(NULL, ":");
    }

    // NULL-terminate for safe iteration.
    result = realloc(result, ((*count + 1) * sizeof(char*)));
    if (result) {
        result[*count] = NULL;
    }
    return result;
}
