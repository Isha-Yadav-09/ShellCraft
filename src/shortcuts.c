#include "my_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

// Shortcut structure
typedef struct {
    char* name;
    char* command;
} Shortcut;

// Global shortcut variables
static Shortcut* shortcuts = NULL;
static int shortcut_count = 0;
static char shortcuts_file_path[256];

void free_shortcuts(Shortcut* shortcuts, int count);

// Initialize shortcuts system (called once at startup)
void init_shortcuts_system(void) {
    const char* home = getenv("HOME");
    if (home) {
        snprintf(shortcuts_file_path, sizeof(shortcuts_file_path), "%s/%s", home, SHORTCUTS_FILE);
    }
    load_shortcuts_from_file(NULL, 0);
}

int command_shortcut(char** args, char** env) {
    (void)env;
    if (args[1] == NULL || args[2] == NULL) {
        printf("Usage: shortcut <name> <command>\n");
        printf("Example: shortcut code \"open -a 'Visual Studio Code'\"\n");
        printf("Example: shortcut notes \"open ~/Notes\"\n");
        return 1;
    }

    const char* name = args[1];
    const char* command = args[2];

    // Check if shortcut already exists
    for (int i = 0; i < shortcut_count; i++) {
        if (strcmp(shortcuts[i].name, name) == 0) {
            printf("Shortcut '%s' already exists!\n", name);
            printf("Use 'remove shortcut %s' to delete it first.\n", name);
            return 1;
        }
    }

    Shortcut* new_shortcuts = realloc(shortcuts, (shortcut_count + 1) * sizeof(Shortcut));
    if (!new_shortcuts) {
        perror("realloc");
        return 1;
    }
    shortcuts = new_shortcuts;

    shortcuts[shortcut_count].name = strdup(name);
    shortcuts[shortcut_count].command = strdup(command);

    if (!shortcuts[shortcut_count].name || !shortcuts[shortcut_count].command) {
        perror("strdup");
        free_shortcuts(shortcuts, shortcut_count);
        free(shortcuts);
        shortcuts = NULL;
        shortcut_count = 0;
        return 1;
    }

    shortcut_count++;

    save_shortcuts_to_file(NULL, shortcuts_file_path);

    printf("Shortcut '%s' created successfully!\n", name);
    printf("Command: %s\n", command);
    return 0;
}

// Expand a leading ~ or $HOME in a command string.
static char* expand_shortcut_command(const char* cmd) {
    if (cmd[0] != '~') {
        return my_strdup(cmd);
    }
    char* home = getenv("HOME");
    if (!home) {
        return my_strdup(cmd);
    }
    const char* rest = cmd + 1;
    char* expanded = malloc(my_strlen(home) + my_strlen(rest) + 2);
    if (!expanded) {
        return NULL;
    }
    if (rest[0] == '/' || rest[0] == '\0') {
        snprintf(expanded, my_strlen(home) + my_strlen(rest) + 2, "%s%s", home, rest);
    } else {
        snprintf(expanded, my_strlen(home) + my_strlen(rest) + 2, "%s/%s", home, rest);
    }
    return expanded;
}

int command_run(char** args, char** env) {
    if (args[1] == NULL) {
        printf("Usage: run <shortcut_name>\n");
        printf("Example: run code\n");
        return 1;
    }

    const char* name = args[1];

    for (int i = 0; i < shortcut_count; i++) {
        if (strcmp(shortcuts[i].name, name) == 0) {
            char* expanded = expand_shortcut_command(shortcuts[i].command);
            if (!expanded) {
                return 1;
            }
            printf("Running shortcut '%s': %s\n", name, expanded);
            char** cmd_args = parse_input(expanded);
            free(expanded);
            if (!cmd_args) {
                return 1;
            }
            int rc = executor(cmd_args, env);
            free_tokens(cmd_args);
            return rc;
        }
    }

    printf("Shortcut '%s' not found!\n", name);
    printf("Use 'list shortcuts' to see all available shortcuts.\n");
    return 1;
}

int command_list_shortcuts(char** args, char** env) {
    (void)args;
    (void)env;
    if (shortcut_count == 0) {
        printf("No shortcuts found.\n");
        printf("Create shortcuts with: shortcut <name> <command>\n");
        return 0;
    }

    printf("Saved Shortcuts (%d):\n", shortcut_count);
    for (int i = 0; i < shortcut_count; i++) {
        printf("  %d. %s -> %s\n", i + 1, shortcuts[i].name, shortcuts[i].command);
    }

    return 0;
}

int command_remove_shortcut(char** args, char** env) {
    (void)env;
    if (args[2] == NULL) {
        printf("Usage: remove shortcut <name>\n");
        return 1;
    }

    const char* name = args[2];

    for (int i = 0; i < shortcut_count; i++) {
        if (strcmp(shortcuts[i].name, name) == 0) {
            printf("Removing shortcut '%s'...\n", name);

            free(shortcuts[i].name);
            free(shortcuts[i].command);

            for (int j = i; j < shortcut_count - 1; j++) {
                shortcuts[j] = shortcuts[j + 1];
            }

            shortcut_count--;

            if (shortcut_count > 0) {
                Shortcut* new_shortcuts = realloc(shortcuts, shortcut_count * sizeof(Shortcut));
                if (new_shortcuts) {
                    shortcuts = new_shortcuts;
                }
            } else {
                free(shortcuts);
                shortcuts = NULL;
            }

            save_shortcuts_to_file(NULL, shortcuts_file_path);

            printf("Shortcut '%s' removed successfully!\n", name);
            return 0;
        }
    }

    printf("Shortcut '%s' not found!\n", name);
    return 1;
}

void save_shortcuts_to_file(char* shortcuts_data, const char* filename) {
    (void)shortcuts_data;
    if (shortcuts_file_path[0] == '\0') {
        const char* home = getenv("HOME");
        if (home) {
            snprintf(shortcuts_file_path, sizeof(shortcuts_file_path), "%s/%s", home, SHORTCUTS_FILE);
        } else {
            printf("Cannot determine shortcuts file location.\n");
            return;
        }
    }

    const char* path = (filename && filename[0]) ? filename : shortcuts_file_path;

    FILE* file = fopen(path, "w");
    if (!file) {
        perror("fopen");
        return;
    }

    for (int i = 0; i < shortcut_count; i++) {
        fprintf(file, "%s|%s\n", shortcuts[i].name, shortcuts[i].command);
    }

    fclose(file);
}

char* load_shortcuts_from_file(char* buffer, size_t buf_size) {
    (void)buffer;
    (void)buf_size;
    if (shortcuts_file_path[0] == '\0') {
        return NULL;
    }

    FILE* file = fopen(shortcuts_file_path, "r");
    if (!file) {
        shortcuts = NULL;
        shortcut_count = 0;
        return NULL;
    }

    char ch;
    int line_count = 0;
    while ((ch = fgetc(file)) != EOF) {
        if (ch == '\n') line_count++;
    }
    rewind(file);

    if (line_count > 0) {
        shortcuts = malloc(line_count * sizeof(Shortcut));
        if (!shortcuts) {
            perror("malloc");
            fclose(file);
            return NULL;
        }

        shortcut_count = 0;
        char line[1024];
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = '\0';
            if (line[0] == '\0') {
                continue;
            }

            char* name = strtok(line, "|");
            char* command = strtok(NULL, "|");

            if (name && command) {
                shortcuts[shortcut_count].name = strdup(name);
                shortcuts[shortcut_count].command = strdup(command);

                if (!shortcuts[shortcut_count].name || !shortcuts[shortcut_count].command) {
                    perror("strdup");
                    free_shortcuts(shortcuts, shortcut_count);
                    free(shortcuts);
                    shortcuts = NULL;
                    shortcut_count = 0;
                    fclose(file);
                    return NULL;
                }

                shortcut_count++;
            }
        }
    } else {
        shortcuts = NULL;
        shortcut_count = 0;
    }

    fclose(file);
    return NULL;
}

void free_shortcuts(Shortcut* shortcuts, int count) {
    for (int i = 0; i < count; i++) {
        free(shortcuts[i].name);
        free(shortcuts[i].command);
    }
}

// Add a shortcut from the config file, skipping it if the name already exists.
void config_add_shortcut(const char* name, const char* cmd) {
    if (!name || !cmd || !*name || !*cmd) return;

    for (int i = 0; i < shortcut_count; i++) {
        if (strcmp(shortcuts[i].name, name) == 0) {
            return; // already present; don't override
        }
    }

    Shortcut* new_shortcuts = realloc(shortcuts, (shortcut_count + 1) * sizeof(Shortcut));
    if (!new_shortcuts) {
        perror("realloc");
        return;
    }
    shortcuts = new_shortcuts;

    shortcuts[shortcut_count].name = strdup(name);
    shortcuts[shortcut_count].command = strdup(cmd);
    if (!shortcuts[shortcut_count].name || !shortcuts[shortcut_count].command) {
        perror("strdup");
        free_shortcuts(shortcuts, shortcut_count);
        free(shortcuts);
        shortcuts = NULL;
        shortcut_count = 0;
        return;
    }
    shortcut_count++;
    save_shortcuts_to_file(NULL, shortcuts_file_path);
}

int shortcut_count_for_completion(void) {
    return shortcut_count;
}

const char* shortcut_name_at(int index) {
    if (index < 0 || index >= shortcut_count) {
        return NULL;
    }
    return shortcuts[index].name;
}

// ---------------------------------------------------------------------------
// File search commands
// ---------------------------------------------------------------------------

int command_find(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        printf("Usage: find <pattern> [path]\n");
        printf("Example: find report.pdf\n");
        return 1;
    }
    const char* pattern = args[1];
    const char* path = args[2] ? args[2] : ".";
    printf("Searching for files containing '%s' under '%s'...\n", pattern, path);
    search_files_by_pattern(pattern, path);
    return 0;
}

int command_search(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        printf("Usage: search <extension> [path]\n");
        printf("Example: search .c\n");
        return 1;
    }
    const char* ext = args[1];
    const char* path = args[2] ? args[2] : ".";
    printf("Searching for '*%s' files under '%s'...\n", ext, path);
    search_files_by_extension(ext, path);
    return 0;
}

int command_recent(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        printf("Usage: recent <directory> [hours]\n");
        printf("Example: recent . 24\n");
        return 1;
    }
    const char* path = args[1];
    time_t since = time(NULL);
    if (args[2]) {
        int hours = atoi(args[2]);
        if (hours > 0) {
            since -= (time_t)hours * 3600;
        }
    } else {
        since -= 24 * 3600;
    }
    printf("Files modified in the last %ldh under '%s':\n",
           (long)((time(NULL) - since) / 3600), path);
    search_recent_files(path, since);
    return 0;
}

void search_files_by_pattern(const char* pattern, const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent* entry;
    char fullpath[MAX_PATH];
    while ((entry = readdir(dir)) != NULL) {
        if (my_strcmp(entry->d_name, ".") == 0 || my_strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        if (strstr(entry->d_name, pattern) != NULL) {
            printf("  %s\n", fullpath);
        }

        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            search_files_by_pattern(pattern, fullpath);
        }
    }
    closedir(dir);
}

void search_files_by_extension(const char* extension, const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    size_t ext_len = my_strlen(extension);
    struct dirent* entry;
    char fullpath[MAX_PATH];
    while ((entry = readdir(dir)) != NULL) {
        if (my_strcmp(entry->d_name, ".") == 0 || my_strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        size_t name_len = my_strlen(entry->d_name);
        if (name_len >= ext_len) {
            const char* tail = entry->d_name + (name_len - ext_len);
            if (my_strcmp(tail, extension) == 0) {
                printf("  %s\n", fullpath);
            }
        }

        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            search_files_by_extension(extension, fullpath);
        }
    }
    closedir(dir);
}

void search_recent_files(const char* path, time_t since_time) {
    DIR* dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent* entry;
    char fullpath[MAX_PATH];
    while ((entry = readdir(dir)) != NULL) {
        if (my_strcmp(entry->d_name, ".") == 0 || my_strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                search_recent_files(fullpath, since_time);
            } else if (st.st_mtime >= since_time) {
                char timebuf[64];
                strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
                printf("  %s  (%s)\n", fullpath, timebuf);
            }
        }
    }
    closedir(dir);
}
