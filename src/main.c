#include "my_shell.h"
#include <editline/readline.h>

// ---------------------------------------------------------------------------
// TAB completion (readline / libedit)
// ---------------------------------------------------------------------------

static const char* BUILTIN_NAMES[] = {
    "cd", "pwd", "echo", "env", "setenv", "unsetenv", "which",
    "shortcut", "run", "list", "remove", "find", "search", "recent",
    "screenshot", "youtube", "leetcode", "spotify", "ai", "claude", "gpt", "do",
    "clip", "open", "weather", "note", "git", "battery", "wifi", "timer",
    "jobs", ".help", "exit", "quit", NULL
};

static char* completion_generator(const char* text, int state) {
    static int bi;
    static int si;
    if (!state) { bi = 0; si = 0; }
    size_t len = strlen(text);

    while (BUILTIN_NAMES[bi]) {
        const char* name = BUILTIN_NAMES[bi++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    int n = shortcut_count_for_completion();
    while (si < n) {
        const char* name = shortcut_name_at(si++);
        if (name && strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return NULL;
}

// Complete with the names of saved shortcuts (for `run`, `shortcut`, `remove`).
static char* shortcut_arg_generator(const char* text, int state) {
    static int idx = 0;
    if (!state) idx = 0;
    size_t len = strlen(text);
    int n = shortcut_count_for_completion();
    while (idx < n) {
        const char* name = shortcut_name_at(idx++);
        if (name && strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return NULL;
}

// Complete with directory names in the current directory (for `cd`).
static char* dir_arg_generator(const char* text, int state) {
    static int idx = 0;
    if (!state) idx = 0;
    size_t len = strlen(text);
    DIR* d = opendir(".");
    if (!d) return NULL;
    struct dirent* e;
    int cur = 0;
    char* found = NULL;
    while ((e = readdir(d)) != NULL) {
        if (my_strcmp(e->d_name, ".") == 0 || my_strcmp(e->d_name, "..") == 0) continue;
        struct stat st;
        if (stat(e->d_name, &st) == 0 && S_ISDIR(st.st_mode) &&
            strncmp(e->d_name, text, len) == 0) {
            if (cur == idx) { found = strdup(e->d_name); break; }
            cur++;
        }
    }
    closedir(d);
    if (found) { idx++; return found; }
    return NULL;
}

// Shared initial working directory, used when executing a resolved command
// (e.g. from the `do` natural-language dispatcher).
static char* g_initial_directory = NULL;

// Parse and run a single command line safely (does not read input, does not
// exit the process). Returns SHELL_EXIT if the line was exit/quit.
int run_shell_line(const char* line, char*** envp) {
    char* copy = my_strdup(line);
    if (!copy) {
        return 1;
    }
    char** args = parse_input(copy);
    free(copy);
    if (!args[0]) {
        free_tokens(args);
        return 0;
    }

    int rc = 0;
    if (my_strcmp(args[0], "setenv") == 0) {
        if (envp) *envp = command_setenv(args, *envp);
    } else if (my_strcmp(args[0], "unsetenv") == 0) {
        if (envp) *envp = command_unsetenv(args, *envp);
    } else if (my_strcmp(args[0], "exit") == 0 || my_strcmp(args[0], "quit") == 0) {
        rc = SHELL_EXIT;
    } else {
        // Route through dispatch_line so pipes / redirection / globs apply,
        // while still letting built-ins (cd, etc.) affect the shell.
        rc = dispatch_line(line, *envp, g_initial_directory);
    }

    free_tokens(args);
    return rc;
}

// Runtime configuration loaded from ~/.myshellrc
int   cfg_ai_provider = AI_AUTO;
char* cfg_ai_model = NULL;
char* cfg_screenshot_mode = NULL;
char* cfg_prompt = NULL;

// Load ~/.myshellrc: default shortcuts, AI provider/model, screenshot mode, prompt.
void load_config(void) {
    const char* home = getenv("HOME");
    if (!home) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.myshellrc", home);
    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        char* key = p;
        char* sp = p;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        char* val = sp;
        if (*sp) { *sp = '\0'; val++; while (*val == ' ' || *val == '\t') val++; }

        if (my_strcmp(key, "shortcut") == 0) {
            char* n = val;
            char* sp2 = val;
            while (*sp2 && *sp2 != ' ' && *sp2 != '\t') sp2++;
            if (*sp2) {
                *sp2 = '\0';
                char* cmd = sp2 + 1;
                while (*cmd == ' ' || *cmd == '\t') cmd++;
                config_add_shortcut(n, cmd);
            }
        } else if (my_strcmp(key, "ai_provider") == 0) {
            if (my_strcmp(val, "anthropic") == 0)      cfg_ai_provider = AI_ANTHROPIC;
            else if (my_strcmp(val, "openai") == 0)    cfg_ai_provider = AI_OPENAI;
            else                                        cfg_ai_provider = AI_AUTO;
        } else if (my_strcmp(key, "ai_model") == 0) {
            free(cfg_ai_model);
            cfg_ai_model = my_strdup(val);
        } else if (my_strcmp(key, "screenshot_mode") == 0) {
            free(cfg_screenshot_mode);
            cfg_screenshot_mode = my_strdup(val);
        } else if (my_strcmp(key, "prompt") == 0) {
            free(cfg_prompt);
            cfg_prompt = my_strdup(val);
        }
    }
    fclose(f);
}

static char** command_completion(const char* text, int start, int end) {
    (void)end;
    if (start == 0) {
        return rl_completion_matches(text, completion_generator);
    }

    // Argument completion: inspect the command word (before `start`).
    char buf[1024];
    size_t blen = (size_t)start < sizeof(buf) ? (size_t)start : sizeof(buf) - 1;
    my_strncpy(buf, rl_line_buffer, blen);
    buf[blen] = '\0';

    char* cmd = buf;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    char* sp = cmd;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    *sp = '\0';

    if (my_strcmp(cmd, "run") == 0 || my_strcmp(cmd, "shortcut") == 0 ||
        my_strcmp(cmd, "remove") == 0) {
        return rl_completion_matches(text, shortcut_arg_generator);
    }
    if (my_strcmp(cmd, "cd") == 0) {
        return rl_completion_matches(text, dir_arg_generator);
    }
    return NULL; // let readline fall back to filename completion
}

// Shell loop
// Input Parsing
// Command execution 
// Handle Built-in commands exp. cd, pwd, echo, env, setenv, unsetenv, which, exit
// Execute external commands
// Manage environment variables
// Manage Path
// Error Handling

void display_help() {
    printf("Available commands:\n");
    printf("\tcd <directory>      - Change the current directory.\n");
    printf("\tpwd                 - Print the current working directory.\n");
    printf("\techo <text>         - Print the given text.\n");
    printf("\tenv                 - Display all environment variables.\n");
    printf("\tsetenv VAR=value    - Set an environment variable.\n");
    printf("\tunsetenv <variable> - Remove an environment variable.\n");
    printf("\twhich <command>     - Locate an executable in the system's PATH.\n");
    printf("\tshortcut <name> <cmd> - Create an application shortcut.\n");
    printf("\trun <name>          - Launch an application shortcut.\n");
    printf("\tlist shortcuts      - List all saved shortcuts.\n");
    printf("\tremove shortcut <name> - Remove a shortcut.\n");
    printf("\tfind <pattern>      - Find files by name pattern.\n");
    printf("\tsearch <ext>        - Find files by extension.\n");
    printf("\trecent <dir> [hrs]  - Find recently modified files.\n");
    printf("\tscreenshot [-i|-w]  - Capture screen (macOS only; needs Screen Recording perm).\n");
    printf("\tyoutube <query>     - Search YouTube in your browser.\n");
    printf("\tleetcode [topic]    - Open a random LeetCode problem.\n");
    printf("\tspotify [play|pause|next|prev|search <q>] - Control Spotify (macOS app only).\n");
    printf("\tai [question]       - Ask an AI (key if set); bare 'ai' opens the provider site.\n");
    printf("\tclaude [question]   - Ask Claude (needs ANTHROPIC_API_KEY); bare 'claude' opens claude.ai.\n");
    printf("\tgpt [question]      - Ask ChatGPT (needs OPENROUTER_API_KEY); bare 'gpt' opens chat.openai.com.\n");
    printf("\tdo <sentence>       - Run a command from plain English.\n");
    printf("\tclip [text]         - Copy text to / paste from the system clipboard (macOS).\n");
    printf("\topen [file]         - Open a file or folder with the default app.\n");
    printf("\tweather [city]      - Show current weather via wttr.in (needs curl + network).\n");
    printf("\tnote [text]         - Save a timestamped note (or print saved notes).\n");
    printf("\tgit <s|l|a|c|p|pl|d> - Quick git shortcuts (status/log/add/commit/push/pull/diff).\n");
    printf("\tbattery             - Show battery charge and status (macOS pmset).\n");
    printf("\twifi                - Show the current Wi-Fi network (macOS airport).\n");
    printf("\ttimer <secs> [msg]  - Set a background countdown that prints when done.\n");
    printf("\tjobs                - List background jobs.\n");
    printf("\t.help               - Display this help message.\n");
    printf("\texit or quit        - Exit the shell.\n");
    printf("\nShell features: pipes (a | b), redirection (> >> < 2>&1),\n");
    printf("  background jobs (cmd &), glob(*.c), ~ expansion, Ctrl-C, TAB completion.\n");
    printf("Config: ~/.myshellrc (prompt, ai_provider, ai_model, screenshot_mode, shortcut).\n");
}

// Built-ins: cd, pwd, echo, env, setenv, unsetenv, which, exit, shortcuts, file search
// Binary: ls, cat.. we'll use executor
int shell_builts(char** args, char** env, char* initial_directory)
{
    if (my_strcmp(args[0], "cd") == 0) {
        return command_cd(args, initial_directory);
    } else if (my_strcmp(args[0], "pwd") == 0) {
        return command_pwd();
    } else if (my_strcmp(args[0], "echo") == 0) {
        return command_echo(args, env);
    } else if (my_strcmp(args[0], "env") == 0) {
        return command_env(env);
    } else if (my_strcmp(args[0], "which") == 0) {
        return command_which(args, env);
    } else if (my_strcmp(args[0], "shortcut") == 0) {
        return command_shortcut(args, env);
    } else if (my_strcmp(args[0], "run") == 0) {
        return command_run(args, env);
    } else if (my_strcmp(args[0], "list") == 0) {
        if (my_strcmp(args[1], "shortcuts") == 0) {
            return command_list_shortcuts(args, env);
        }
    } else if (my_strcmp(args[0], "remove") == 0) {
        if (my_strcmp(args[1], "shortcut") == 0) {
            return command_remove_shortcut(args, env);
        }
    } else if (my_strcmp(args[0], "find") == 0) {
        return command_find(args, env);
    } else if (my_strcmp(args[0], "search") == 0) {
        return command_search(args, env);
    } else if (my_strcmp(args[0], "recent") == 0) {
        return command_recent(args, env);
    } else if (my_strcmp(args[0], "screenshot") == 0) {
        return command_screenshot(args, env);
    } else if (my_strcmp(args[0], "youtube") == 0) {
        return command_youtube(args, env);
    } else if (my_strcmp(args[0], "leetcode") == 0) {
        return command_leetcode(args, env);
    } else if (my_strcmp(args[0], "spotify") == 0) {
        return command_spotify(args, env);
    } else if (my_strcmp(args[0], "ai") == 0) {
        return command_ai(args, env);
    } else if (my_strcmp(args[0], "claude") == 0) {
        return command_claude(args, env);
    } else if (my_strcmp(args[0], "gpt") == 0) {
        return command_gpt(args, env);
    } else if (my_strcmp(args[0], "jobs") == 0) {
        return command_jobs(args, env);
    } else if (my_strcmp(args[0], "do") == 0) {
        return command_do(args, env);
    } else if (my_strcmp(args[0], "clip") == 0) {
        return command_clip(args, env);
    } else if (my_strcmp(args[0], "open") == 0) {
        return command_open(args, env);
    } else if (my_strcmp(args[0], "weather") == 0) {
        return command_weather(args, env);
    } else if (my_strcmp(args[0], "note") == 0) {
        return command_note(args, env);
    } else if (my_strcmp(args[0], "git") == 0) {
        return command_git(args, env);
    } else if (my_strcmp(args[0], "battery") == 0) {
        return command_battery(args, env);
    } else if (my_strcmp(args[0], "wifi") == 0) {
        return command_wifi(args, env);
    } else if (my_strcmp(args[0], "timer") == 0) {
        return command_timer(args, env);
    } else if (my_strcmp(args[0], ".help") == 0) {
        display_help();
        return 0;
    } else if (my_strcmp(args[0], "exit") == 0 || my_strcmp(args[0], "quit") == 0) {
        exit(EXIT_SUCCESS);
    } else {
        // Not a built-in command, execute as external command
        return executor(args, env);
    }
    return 0;
}

void shell_loop(char** env)
{
    char* initial_directory = getcwd(NULL, 0);
    g_initial_directory = initial_directory;
    init_job_control();
    int interactive = isatty(STDIN_FILENO);
    char hist_path[MAX_PATH];
    const char* home = getenv("HOME");
    int have_hist = 0;

    if (interactive) {
        rl_attempted_completion_function = command_completion;
        using_history();
        if (home) {
            snprintf(hist_path, sizeof(hist_path), "%s/.myshell_history", home);
            read_history(hist_path);
            have_hist = 1;
        }
    }

    printf("Type .help for a list of available commands.\n");

    const char* prompt = cfg_prompt ? cfg_prompt : "[my_shell]> ";

    char* line = NULL;
    size_t line_cap = 0;

    while (1)
    {
        if (interactive) {
            line = readline(prompt);
            if (line == NULL) {
                printf("\n");
                break;
            }
        } else {
            printf("%s ", prompt);
            fflush(stdout);
            if (getline(&line, &line_cap, stdin) == -1) {
                printf("\n");
                break;
            }
            line[strcspn(line, "\n")] = '\0';
        }

        if (line[0] == '\0') {
            if (interactive) free(line);
            continue;
        }
        if (interactive) add_history(line);

        char** args = parse_input(line);

        if (!args[0]) {
            free_tokens(args);
        } else if (my_strcmp(args[0], "setenv") == 0) {
            env = command_setenv(args, env);
        } else if (my_strcmp(args[0], "unsetenv") == 0) {
            env = command_unsetenv(args, env);
        } else {
            dispatch_line(line, env, initial_directory);
        }

        free_tokens(args);
        reap_background_jobs();
        if (interactive) free(line);
    }

    if (interactive && have_hist) {
        write_history(hist_path);
    }
    free(initial_directory);
}

int main (int argc, char** argv, char** env)
{
    (void)argc;
    (void)argv;

    init_shortcuts_system();
    load_config();

    shell_loop(env);

    return 0;
}