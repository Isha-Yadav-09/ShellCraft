#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_INPUT 1024
#define SHORTCUTS_FILE ".myshell_shortcuts"
#define MAX_PATH 4096
#define MAX_FILENAME 256
#define SHELL_EXIT 777  // returned by dispatcher when exit/quit is requested

// AI provider selection (shared between tools.c and config loader)
#define AI_AUTO      0
#define AI_ANTHROPIC 1
#define AI_OPENAI    2

// Input Parser
char** parse_input      (char* input);
void free_tokens        (char** tokens);

// Built-in function implementations
int command_cd          (char** args, char* initial_directory);
int command_pwd         ();
int command_echo        (char** args, char** env);
int command_env         (char** env);
int command_which       (char** args, char** env);
char** command_setenv   (char** args, char** env);
char** command_unsetenv (char** args, char** env);

// Shortcut functions
void init_shortcuts_system(void);
int command_shortcut      (char** args, char** env);
int command_run          (char** args, char** env);
int command_list_shortcuts(char** args, char** env);
int command_remove_shortcut(char** args, char** env);
void save_shortcuts_to_file(char* shortcuts_data, const char* filename);
char* load_shortcuts_from_file(char* buffer, size_t buf_size);
int  shortcut_count_for_completion(void);
const char* shortcut_name_at(int index);

// File search functions
int command_find      (char** args, char** env);
int command_search    (char** args, char** env);
int command_recent    (char** args, char** env);
void search_files_by_pattern(const char* pattern, const char* path);
void search_files_by_extension(const char* extension, const char* path);
void search_recent_files(const char* path, time_t since_time);

// Executor
int executor            (char** args, char** env);
int child_process       (char** args, char** env);
char* resolve_exec      (const char* cmd, char** env);
int is_builtin          (const char* name);
int dispatch_line       (const char* line, char** env, char* initial_directory);
int run_shell_line      (const char* line, char*** envp);
int shell_builts        (char** args, char** env, char* initial_directory);
void init_job_control   (void);
void reap_background_jobs(void);

// Path functions
char* get_path          (char** env);
char** split_paths      (char* paths, int* count);

// Helpers
int my_strcmp           (const char* str1, const char* str2);
int my_strlen           (const char* str);
int my_strncmp          (const char* str1, const char* str2, size_t n);
char* my_getenv         (const char *name, char**env);
char* my_strdup         (const char* str);
char* my_strcpy         (char* dest, const char* src);
char* my_strchr         (const char* str, char c);
char* my_strtok         (char* input_string, const char* delimiter);
char* my_strncpy        (char* dest, const char* src, size_t n);
// Utility functions
int contains_wildcard(const char* str);
void replace_wildcards_in_path(char* buffer, size_t buf_size, const char* template, const char* arg);

// Tools (macOS helpers): screenshot, youtube, leetcode, music, ai, do
int command_screenshot (char** args, char** env);
int command_youtube    (char** args, char** env);
int command_leetcode   (char** args, char** env);
int command_spotify    (char** args, char** env);
int command_ai         (char** args, char** env);
int command_claude     (char** args, char** env);
int command_gpt        (char** args, char** env);
int command_do         (char** args, char** env);
int command_jobs       (char** args, char** env);
int command_clip        (char** args, char** env);
int command_open        (char** args, char** env);
int command_weather     (char** args, char** env);
int command_note        (char** args, char** env);
int command_git         (char** args, char** env);
int command_battery     (char** args, char** env);
int command_wifi        (char** args, char** env);
int command_timer       (char** args, char** env);

// Config file ~/.myshellrc
void load_config       (void);
void config_add_shortcut(const char* name, const char* cmd);

// Runtime configuration (set by load_config, read by various commands)
extern int   cfg_ai_provider;   // AI_AUTO / AI_ANTHROPIC / AI_OPENAI
extern char* cfg_ai_model;      // default model for `ai`
extern char* cfg_screenshot_mode; // default mode for `screenshot`
extern char* cfg_prompt;         // custom prompt string
