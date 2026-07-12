#include "my_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

// Join args[start..] into a single space-separated string. Caller frees.
static char* join_args_local(char** args, int start) {
    size_t cap = 256;
    char* buf = malloc(cap);
    if (!buf) return NULL;
    size_t len = 0;
    buf[0] = '\0';
    for (int i = start; args[i]; i++) {
        size_t alen = my_strlen(args[i]);
        if (len + alen + 2 >= cap) {
            cap = len + alen + 256;
            char* g = realloc(buf, cap);
            if (!g) { free(buf); return NULL; }
            buf = g;
        }
        if (len > 0) buf[len++] = ' ';
        my_strcpy(buf + len, args[i]);
        len += alen;
    }
    return buf;
}

// ---------------------------------------------------------------------------
// clip — copy to / paste from the system clipboard (macOS pbcopy/pbpaste)
// ---------------------------------------------------------------------------

int command_clip(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        if (system("command -v pbpaste >/dev/null 2>&1") != 0) {
            printf("Clipboard paste needs macOS (pbpaste) on this system.\n");
            return 1;
        }
        return system("pbpaste") == 0 ? 0 : 1;
    }
    if (system("command -v pbcopy >/dev/null 2>&1") != 0) {
        printf("Clipboard copy needs macOS (pbcopy) on this system.\n");
        return 1;
    }
    char* text = join_args_local(args, 1);
    if (!text) return 1;
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "pbcopy <<'MYSHELL_CLIP_EOF'\n%s\nMYSHELL_CLIP_EOF", text);
    free(text);
    return system(cmd) == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// open — open a file/folder with the default application
// ---------------------------------------------------------------------------

int command_open(char** args, char** env) {
    (void)env;
    const char* target = args[1] ? args[1] : ".";
    char opener[32];
    if (system("command -v open >/dev/null 2>&1") == 0) {
        my_strcpy(opener, "open");
    } else if (system("command -v xdg-open >/dev/null 2>&1") == 0) {
        my_strcpy(opener, "xdg-open");
    } else {
        printf("No 'open' or 'xdg-open' available to open files here.\n");
        return 1;
    }
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "%s \"%s\"", opener, target);
    return system(cmd) == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// weather — show the current weather via wttr.in
// ---------------------------------------------------------------------------

int command_weather(char** args, char** env) {
    (void)env;
    char url[MAX_PATH];
    if (args[1]) {
        snprintf(url, sizeof(url), "https://wttr.in/%s?0", args[1]);
    } else {
        snprintf(url, sizeof(url), "https://wttr.in/?0");
    }
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "curl -s --max-time 20 '%s'", url);
    int rc = system(cmd);
    if (rc != 0) {
        printf("Could not fetch weather — needs network access and curl.\n");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// note — quick-capture a timestamped note, or print saved notes
// ---------------------------------------------------------------------------

int command_note(char** args, char** env) {
    (void)env;
    const char* home = getenv("HOME");
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.myshell_notes.md", home ? home : ".");

    if (args[1] == NULL) {
        if (access(path, F_OK) != 0) {
            printf("No notes yet. Capture one with: note <text>\n");
            return 0;
        }
        char cmd[MAX_PATH + 32];
        snprintf(cmd, sizeof(cmd), "cat '%s'", path);
        return system(cmd) == 0 ? 0 : 1;
    }

    char* text = join_args_local(args, 1);
    if (!text) return 1;
    time_t t = time(NULL);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", localtime(&t));
    FILE* f = fopen(path, "a");
    if (!f) {
        perror("fopen");
        free(text);
        return 1;
    }
    fprintf(f, "- [%s] %s\n", stamp, text);
    fclose(f);
    printf("Note saved.\n");
    free(text);
    return 0;
}

// ---------------------------------------------------------------------------
// git — common git shortcuts (status/log/add/commit/push/pull/diff)
// ---------------------------------------------------------------------------

int command_git(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        return system("git status") == 0 ? 0 : 1;
    }
    const char* sub = args[1];
    char cmd[MAX_PATH];
    if (my_strcmp(sub, "s") == 0 || my_strcmp(sub, "status") == 0) {
        snprintf(cmd, sizeof(cmd), "git status");
    } else if (my_strcmp(sub, "l") == 0 || my_strcmp(sub, "log") == 0) {
        snprintf(cmd, sizeof(cmd), "git log --oneline -15");
    } else if (my_strcmp(sub, "a") == 0 || my_strcmp(sub, "add") == 0) {
        snprintf(cmd, sizeof(cmd), "git add -A");
    } else if (my_strcmp(sub, "c") == 0 || my_strcmp(sub, "commit") == 0) {
        char* msg = join_args_local(args, 2);
        if (!msg || !*msg) {
            printf("Usage: git c <message>\n");
            free(msg);
            return 1;
        }
        snprintf(cmd, sizeof(cmd), "git commit -m \"%s\"", msg);
        free(msg);
    } else if (my_strcmp(sub, "p") == 0 || my_strcmp(sub, "push") == 0) {
        snprintf(cmd, sizeof(cmd), "git push");
    } else if (my_strcmp(sub, "pl") == 0 || my_strcmp(sub, "pull") == 0) {
        snprintf(cmd, sizeof(cmd), "git pull");
    } else if (my_strcmp(sub, "d") == 0 || my_strcmp(sub, "diff") == 0) {
        snprintf(cmd, sizeof(cmd), "git diff");
    } else {
        char* rest = join_args_local(args, 1);
        snprintf(cmd, sizeof(cmd), "git %s", rest ? rest : "");
        free(rest);
    }
    return system(cmd) == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// battery — show battery charge / status (macOS pmset)
// ---------------------------------------------------------------------------

int command_battery(char** args, char** env) {
    (void)args;
    (void)env;
    if (system("command -v pmset >/dev/null 2>&1") != 0) {
        printf("Battery status is only available via 'pmset' (macOS) here.\n");
        return 0;
    }
    FILE* p = popen("pmset -g batt 2>/dev/null", "r");
    if (!p) {
        printf("Battery status unavailable.\n");
        return 0;
    }
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, "%")) {
            line[strcspn(line, "\n")] = '\0';
            printf("Battery: %s\n", line);
            found = 1;
        }
    }
    pclose(p);
    if (!found) printf("No battery information found.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// wifi — show the current Wi-Fi network (macOS airport)
// ---------------------------------------------------------------------------

int command_wifi(char** args, char** env) {
    (void)args;
    (void)env;
    const char* airport =
        "/System/Library/PrivateFrameworks/Apple80211.framework/"
        "Versions/Current/Resources/airport";
    if (access(airport, X_OK) != 0) {
        printf("Wi-Fi info is only available via 'airport' (macOS) here.\n");
        return 0;
    }
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "%s -I | grep -E 'SSID|MCS|lastTxRate|agrCtlRSSI'", airport);
    return system(cmd) == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// timer — set a background countdown that prints when finished
// ---------------------------------------------------------------------------

int command_timer(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        printf("Usage: timer <seconds> [message]\n");
        return 1;
    }
    int secs = atoi(args[1]);
    if (secs <= 0) {
        printf("Please give a positive number of seconds.\n");
        return 1;
    }
    char* msg = join_args_local(args, 2);
    char cmd[1024];
    if (msg && *msg) {
        snprintf(cmd, sizeof(cmd),
                 "( sleep %d; printf '\\n[timer] %s\\n' ) &\n", secs, msg);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "( sleep %d; printf '\\n[timer] %d seconds finished\\n' ) &\n",
                 secs, secs);
    }
    printf("Timer set for %d seconds.\n", secs);
    system(cmd);
    free(msg);
    return 0;
}
