#include "my_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Runtime configuration (defined in main.c, loaded from ~/.myshellrc)
extern int   cfg_ai_provider;
extern char* cfg_ai_model;
extern char* cfg_screenshot_mode;

// Join args[start..] into a single space-separated string. Caller frees.
static char* join_args(char** args, int start) {
    size_t cap = 256;
    char* buf = malloc(cap);
    if (!buf) {
        return NULL;
    }
    size_t len = 0;
    buf[0] = '\0';

    for (int i = start; args[i]; i++) {
        size_t alen = my_strlen(args[i]);
        if (len + alen + 2 >= cap) {
            cap = len + alen + 256;
            char* grown = realloc(buf, cap);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
        }
        if (len > 0) {
            buf[len++] = ' ';
        }
        my_strcpy(buf + len, args[i]);
        len += alen;
    }
    return buf;
}

// Replace spaces with '+' (URL-query style) in place.
static void spaces_to_plus(char* s) {
    for (; *s; s++) {
        if (*s == ' ') {
            *s = '+';
        }
    }
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

// True if a path exists (file or directory).
static int path_exists(const char* p) {
    return access(p, F_OK) == 0;
}

// True if a regular file exists at path.
static int file_exists(const char* p) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

static void take_screenshot(const char* mode) {
    char path[MAX_PATH];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", t);

    const char* home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/Desktop/screenshot-%s.png",
             home ? home : ".", stamp);

    // Screenshots are a macOS-only feature (screencapture).
    if (system("command -v screencapture >/dev/null 2>&1") != 0) {
        printf("Screenshots are only supported on macOS (which provides the\n");
        printf("'screencapture' command). This system doesn't appear to have it,\n");
        printf("so the screenshot command can't work here.\n");
        return;
    }

    char cmd[MAX_PATH + 64];
    if (mode && my_strcmp(mode, "-i") == 0) {
        snprintf(cmd, sizeof(cmd), "screencapture -i %s", path);
    } else if (mode && my_strcmp(mode, "-w") == 0) {
        snprintf(cmd, sizeof(cmd), "screencapture -w %s", path);
    } else {
        snprintf(cmd, sizeof(cmd), "screencapture -x %s", path);
    }

    printf("Capturing screenshot to %s ...\n", path);
    int rc = system(cmd);
    if (rc != 0 || !file_exists(path)) {
        printf("\nScreenshot did not complete. Common reasons:\n");
        printf("  • Screen Recording permission is not granted to this terminal app.\n");
        printf("    Fix: System Settings -> Privacy & Security -> Screen Recording,\n");
        printf("    enable this terminal (Terminal.app / iTerm / the app running\n");
        printf("    my_shell), then fully quit and reopen the terminal.\n");
        printf("  • No screen to capture: you're in a non-graphical session\n");
        printf("    (SSH / headless), so there's nothing to grab.\n");
        printf("  • You cancelled the capture (e.g. pressed Esc in -i mode).\n");
    } else {
        printf("Saved: %s\n", path);
    }
}

int command_screenshot(char** args, char** env) {
    (void)env;
    take_screenshot(args[1] ? args[1] : cfg_screenshot_mode);
    return 0;
}

// ---------------------------------------------------------------------------
// YouTube
// ---------------------------------------------------------------------------

static void open_youtube(const char* query) {
    char url[MAX_PATH];
    if (query && query[0]) {
        char* q = my_strdup(query);
        spaces_to_plus(q);
        snprintf(url, sizeof(url), "https://www.youtube.com/results?search_query=%s", q);
        free(q);
    } else {
        snprintf(url, sizeof(url), "https://www.youtube.com/");
    }
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
    printf("Opening YouTube: %s\n", url);
    system(cmd);
}

int command_youtube(char** args, char** env) {
    (void)env;
    char* query = join_args(args, 1);
    if (!query) {
        return 1;
    }
    open_youtube(query);
    free(query);
    return 0;
}

// ---------------------------------------------------------------------------
// LeetCode
// ---------------------------------------------------------------------------

// Fetch the problem list and open a random problem (LeetCode removed its
// static "random" URL, so we pick one ourselves).
static void open_random_leetcode(void) {
    printf("Picking a random LeetCode problem...\n");

    FILE* pipe = popen("curl -s --max-time 25 -A 'Mozilla/5.0' https://leetcode.com/api/problems/all/", "r");
    if (!pipe) {
        perror("popen");
        return;
    }

    size_t cap = 4 * 1024 * 1024;
    char* buf = malloc(cap);
    if (!buf) {
        pclose(pipe);
        return;
    }
    size_t total = 0;
    size_t n;
    while ((n = fread(buf + total, 1, cap - total - 1, pipe)) > 0) {
        total += n;
        if (total >= cap - 1) break;
    }
    buf[total] = '\0';
    pclose(pipe);

    const char* marker = "\"question__title_slug\"";
    size_t mlen = my_strlen(marker);

    int count = 0;
    for (size_t i = 0; buf[i]; i++) {
        if (my_strncmp(buf + i, marker, mlen) == 0) {
            size_t k = i + mlen;
            while (buf[k] && buf[k] != ':') k++;
            if (buf[k] == ':') {
                k++;
                while (buf[k] == ' ' || buf[k] == '\t') k++;
                if (buf[k] == '"') count++;
            }
            i += mlen;
        }
    }

    if (count == 0) {
        printf("Could not fetch problems from LeetCode. Opening the list instead.\n");
        char cmd[MAX_PATH + 32];
        snprintf(cmd, sizeof(cmd), "open \"https://leetcode.com/problemset/\"");
        system(cmd);
        free(buf);
        return;
    }

    char** slugs = malloc((count + 1) * sizeof(char*));
    if (!slugs) {
        free(buf);
        return;
    }
    int idx = 0;
    for (size_t i = 0; buf[i] && idx < count; i++) {
        if (my_strncmp(buf + i, marker, mlen) == 0) {
            size_t k = i + mlen;
            while (buf[k] && buf[k] != ':') k++;
            if (buf[k] == ':') {
                k++;
                while (buf[k] == ' ' || buf[k] == '\t') k++;
                if (buf[k] == '"') {
                    k++;
                    char* slug = malloc(128);
                    if (!slug) break;
                    int j = 0;
                    while (buf[k] && buf[k] != '"' && j < 127) {
                        slug[j++] = buf[k++];
                    }
                    slug[j] = '\0';
                    slugs[idx++] = slug;
                }
            }
            i += mlen;
        }
    }

    srand((unsigned int)time(NULL));
    int pick = rand() % count;

    char url[MAX_PATH];
    snprintf(url, sizeof(url), "https://leetcode.com/problems/%s/", slugs[pick]);

    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
    printf("Opening LeetCode problem: %s\n", url);
    system(cmd);

    for (int i = 0; i < count; i++) free(slugs[i]);
    free(slugs);
    free(buf);
}

static void open_leetcode(const char* tag) {
    if (tag && tag[0]) {
        char* t = my_strdup(tag);
        spaces_to_plus(t);
        char url[MAX_PATH];
        snprintf(url, sizeof(url),
                 "https://leetcode.com/problemset/?topicSlugs=[\"%s\"]", t);
        free(t);
        char cmd[MAX_PATH + 32];
        snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
        printf("Opening LeetCode (topic): %s\n", url);
        system(cmd);
    } else {
        open_random_leetcode();
    }
}

int command_leetcode(char** args, char** env) {
    (void)env;
    char* tag = join_args(args, 1);
    if (!tag) {
        return 1;
    }
    open_leetcode(tag);
    free(tag);
    return 0;
}

// ---------------------------------------------------------------------------
// Music (macOS Music app via AppleScript)
// ---------------------------------------------------------------------------

// Run an AppleScript from a file and capture combined output + exit status.
// Returns 0 if the run itself worked (the script may still report an error).
static int run_osascript(const char* script, char* out, size_t outsize, int* exit_code) {
    out[0] = '\0';
    const char* home = getenv("HOME");
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.myshell_spotify.scpt", home ? home : ".");
    FILE* f = fopen(path, "w");
    if (!f) { *exit_code = -1; return -1; }
    fputs(script, f);
    fclose(f);

    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "osascript '%s' 2>&1", path);
    FILE* pipe = popen(cmd, "r");
    if (!pipe) { *exit_code = -1; unlink(path); return -1; }
    size_t total = 0, n;
    char buf[2048];
    while ((n = fread(buf, 1, sizeof(buf) - 1, pipe)) > 0) {
        if (total + n < outsize - 1) { my_strncpy(out + total, buf, n); total += n; }
    }
    out[total] = '\0';
    int st = pclose(pipe);
    *exit_code = (st >= 0) ? WEXITSTATUS(st) : -1;
    unlink(path);
    return 0;
}

static void spotify_control(const char* action, const char* query) {
    char script[MAX_PATH];
    if (my_strcmp(action, "play") == 0) {
        snprintf(script, sizeof(script), "tell application \"Spotify\" to play");
    } else if (my_strcmp(action, "pause") == 0) {
        snprintf(script, sizeof(script), "tell application \"Spotify\" to pause");
    } else if (my_strcmp(action, "next") == 0) {
        snprintf(script, sizeof(script), "tell application \"Spotify\" to next track");
    } else if (my_strcmp(action, "prev") == 0) {
        snprintf(script, sizeof(script), "tell application \"Spotify\" to previous track");
    } else if (my_strcmp(action, "search") == 0 && query && query[0]) {
        snprintf(script, sizeof(script),
                 "tell application \"Spotify\" to play track \"spotify:search:%s\"",
                 query);
    } else {
        printf("Usage: spotify [play|pause|next|prev|search <query>]\n");
        return;
    }

    int installed = path_exists("/Applications/Spotify.app") ||
                    path_exists("/System/Applications/Spotify.app");

    char out[1024];
    int exit_code = 0;
    run_osascript(script, out, sizeof(out), &exit_code);

    if (exit_code == 0 && out[0] == '\0') {
        printf("Spotify: %s%s%s\n", action, query ? " " : "", query ? query : "");
        return;
    }

    printf("\nSpotify '%s' did not run. Why this can fail:\n", action);
    if (strstr(out, "not authorized") || strstr(out, "Not allowed") ||
        strstr(out, "(-1743)") || strstr(out, "automation")) {
        printf("  • macOS blocked it. Grant permission:\n");
        printf("      System Settings -> Privacy & Security -> Automation ->\n");
        printf("      [this terminal] and turn on Spotify.\n");
    } else if (strstr(out, "isn't running") || strstr(out, "(-600)")) {
        printf("  • Spotify isn't running. Open the Spotify desktop app first.\n");
    } else if (!installed || strstr(out, "can't be found") || strstr(out, "(-10814)")) {
        printf("  • The Spotify desktop app doesn't appear to be installed\n");
        printf("    (looked in /Applications/Spotify.app). Install it from\n");
        printf("    https://spotify.com — the web player can't be controlled.\n");
    } else {
        printf("  • Unexpected error from AppleScript: %s\n", out);
    }
    printf("  (Note: controlling Spotify requires the macOS desktop app and\n");
    printf("   Apple 'Automation' permission. It is a macOS-only feature.)\n");
}

int command_spotify(char** args, char** env) {
    (void)env;
    const char* sub = args[1];
    if (sub == NULL || my_strcmp(sub, "play") == 0) {
        spotify_control("play", NULL);
    } else if (my_strcmp(sub, "pause") == 0) {
        spotify_control("pause", NULL);
    } else if (my_strcmp(sub, "next") == 0) {
        spotify_control("next", NULL);
    } else if (my_strcmp(sub, "prev") == 0 || my_strcmp(sub, "previous") == 0) {
        spotify_control("prev", NULL);
    } else if (my_strcmp(sub, "search") == 0 && args[2]) {
        char* q = join_args(args, 2);
        spotify_control("search", q ? q : "");
        free(q);
    } else {
        printf("Usage: spotify [play|pause|next|prev|search <query>]\n");
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// AI chat
// ---------------------------------------------------------------------------

// Minimal JSON escaping for a string (handles " \ and newlines).
static void json_escape(const char* in, char* out, size_t outsize) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outsize; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') {
            out[j++] = ' ';
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

// Pull the first "key":"value" string from a JSON fragment (shallow scan).
static void extract_json_string(const char* json, const char* key, char* out, size_t outsize) {
    out[0] = '\0';
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return;
    p += my_strlen(pat);
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return;
    p++;
    size_t j = 0;
    while (*p && *p != '"' && j + 1 < outsize) {
        if (*p == '\\' && *(p + 1)) {
            char n = *(p + 1);
            if (n == 'n') out[j++] = '\n';
            else if (n == 't') out[j++] = '\t';
            else if (n == '"') out[j++] = '"';
            else if (n == '\\') out[j++] = '\\';
            else out[j++] = n;
            p += 2;
        } else {
            out[j++] = *p++;
        }
    }
    out[j] = '\0';
}

// Build the "messages" JSON array (system + optional saved history + current turn).
// hist_path may be NULL to skip loading prior context.
static char* build_messages_json(const char* system_prompt, const char* current, const char* hist_path) {
    size_t cap = 32768;
    char* out = malloc(cap);
    if (!out) return NULL;
    out[0] = '\0';
    size_t len = 0;

    char sys_esc[4096];
    json_escape(system_prompt, sys_esc, sizeof(sys_esc));
    len += (size_t)snprintf(out + len, cap - len,
        "[{\"role\":\"system\",\"content\":\"%s\"}", sys_esc);

    if (hist_path) {
        FILE* f = fopen(hist_path, "r");
        if (f) {
            char line[2048];
            char user_buf[4096];
            int have_user = 0;
            while (fgets(line, sizeof(line), f) && len < cap - 1024) {
                line[strcspn(line, "\n")] = '\0';
                if (my_strncmp(line, "USER:", 5) == 0) {
                    my_strcpy(user_buf, line + 5);
                    have_user = 1;
                } else if (my_strncmp(line, "ASSISTANT:", 10) == 0 && have_user) {
                    char eu[4096], ea[4096];
                    json_escape(user_buf, eu, sizeof(eu));
                    json_escape(line + 10, ea, sizeof(ea));
                    len += (size_t)snprintf(out + len, cap - len,
                        ",{\"role\":\"user\",\"content\":\"%s\"},{\"role\":\"assistant\",\"content\":\"%s\"}",
                        eu, ea);
                    have_user = 0;
                }
            }
            fclose(f);
        }
    }

    char esc_cur[4096];
    json_escape(current, esc_cur, sizeof(esc_cur));
    len += (size_t)snprintf(out + len, cap - len,
        ",{\"role\":\"user\",\"content\":\"%s\"}]", esc_cur);
    return out;
}

// Generic completion. Returns 1 if content was received (stored in out),
// 0 if none, -1 if the required API key is not configured.
static int ai_complete(const char* system_prompt, const char* user_prompt,
                       const char* hist_path, int provider, char* out, size_t outsize) {
    out[0] = '\0';

    const char* anthropic_key = getenv("ANTHROPIC_API_KEY");
    const char* openrouter_key = getenv("OPENROUTER_API_KEY");
    const char* model = cfg_ai_model ? cfg_ai_model : getenv("AI_MODEL");

    int use_anthropic;
    const char* key;
    if (provider == AI_ANTHROPIC) {
        use_anthropic = 1; key = anthropic_key;
    } else if (provider == AI_OPENAI) {
        use_anthropic = 0; key = openrouter_key;
    } else { // AI_AUTO
        use_anthropic = (anthropic_key != NULL);
        key = use_anthropic ? anthropic_key : openrouter_key;
    }
    if (!key) {
        return -1;
    }

    const char* model_used = model ? model
                          : (use_anthropic ? "claude-3-5-sonnet-latest" : "openai/gpt-3.5-turbo");

    char* messages = build_messages_json(system_prompt, user_prompt, hist_path);
    if (!messages) {
        return 0;
    }

    const char* home = getenv("HOME");
    char req_path[MAX_PATH];
    snprintf(req_path, sizeof(req_path), "%s/.myshell_ai_req.json", home ? home : ".");
    FILE* tf = fopen(req_path, "w");
    if (!tf) {
        perror("fopen");
        free(messages);
        return 0;
    }
    if (use_anthropic) {
        fprintf(tf, "{\"model\":\"%s\",\"max_tokens\":1024,\"stream\":true,\"messages\":%s}", model_used, messages);
    } else {
        fprintf(tf, "{\"model\":\"%s\",\"stream\":true,\"messages\":%s}", model_used, messages);
    }
    fclose(tf);
    free(messages);

    char curl_cmd[2048];
    if (use_anthropic) {
        snprintf(curl_cmd, sizeof(curl_cmd),
            "curl -sN --max-time 30 -X POST https://api.anthropic.com/v1/messages "
            "-H 'Content-Type: application/json' -H 'x-api-key: %s' "
            "-H 'anthropic-version: 2023-06-01' -d @%s", key, req_path);
    } else {
        snprintf(curl_cmd, sizeof(curl_cmd),
            "curl -sN --max-time 30 -X POST https://openrouter.ai/api/v1/chat/completions "
            "-H 'Content-Type: application/json' -H 'Authorization: Bearer %s' -d @%s", key, req_path);
    }

    FILE* pipe = popen(curl_cmd, "r");
    if (!pipe) {
        perror("popen");
        return 0;
    }
    char line[8192];
    size_t olen = 0;
    int any = 0;
    while (fgets(line, sizeof(line), pipe)) {
        line[strcspn(line, "\n")] = '\0';
        if (my_strncmp(line, "data:", 5) != 0) continue;
        char* json = line + 5;
        while (*json == ' ') json++;
        if (my_strncmp(json, "[DONE]", 6) == 0) break;
        char piece[8192];
        extract_json_string(json, "content", piece, sizeof(piece));
        if (piece[0] == '\0') extract_json_string(json, "text", piece, sizeof(piece));
        if (piece[0]) {
            any = 1;
            size_t pl = my_strlen(piece);
            if (olen + pl + 1 < outsize) {
                my_strcpy(out + olen, piece);
                olen += pl;
            }
        }
    }
    pclose(pipe);
    return any ? 1 : 0;
}

static void persist_ai(const char* hist_path, const char* prompt, const char* resp) {
    char buf[16384];
    my_strcpy(buf, resp);
    for (size_t i = 0; buf[i]; i++) {
        if (buf[i] == '\n') buf[i] = ' ';
    }
    FILE* hf = fopen(hist_path, "a");
    if (hf) {
        fprintf(hf, "USER:%s\nASSISTANT:%s\n", prompt, buf);
        fclose(hf);
    }
}

// Run a chat turn against a specific provider, with its own history file.
static void chat_provider(const char* prompt, int provider, const char* hist_name,
                           const char* label, const char* browser_url) {
    const char* key = (provider == AI_ANTHROPIC) ? getenv("ANTHROPIC_API_KEY")
                                                 : getenv("OPENROUTER_API_KEY");
    if (!key) {
        if (provider == AI_ANTHROPIC) {
            printf("No ANTHROPIC_API_KEY set. Set it to use the %s command.\n", label);
        } else {
            printf("No OPENROUTER_API_KEY set. Set it to use the %s command.\n", label);
        }
        printf("Opening your browser instead:\n  %s\n", prompt);
        char cmd[MAX_PATH + 32];
        snprintf(cmd, sizeof(cmd), "open \"%s\"", browser_url);
        system(cmd);
        return;
    }

    char hist_path[MAX_PATH];
    const char* home = getenv("HOME");
    snprintf(hist_path, sizeof(hist_path), "%s/%s", home ? home : ".", hist_name);

    char buf[16384];
    int ok = ai_complete("You are a helpful assistant inside my_shell, a custom macOS shell.",
                         prompt, hist_path, provider, buf, sizeof(buf));
    if (ok == 0) {
        printf("(No response from %s. Check your API key, AI_MODEL, and network.)\n", label);
        return;
    }

    printf("\n%s: %s\n", label, buf);
    persist_ai(hist_path, prompt, buf);
}

static void ask_ai(const char* prompt) {
    const char* home = getenv("HOME");
    char hist_path[MAX_PATH];
    snprintf(hist_path, sizeof(hist_path), "%s/.myshell_ai_history", home ? home : ".");

    int provider = (cfg_ai_provider != AI_AUTO) ? cfg_ai_provider : AI_AUTO;
    char buf[16384];
    int ok = ai_complete("You are a helpful assistant inside my_shell, a custom macOS shell.",
                         prompt, hist_path, provider, buf, sizeof(buf));
    if (ok == -1) {
        printf("No API key set. Set ANTHROPIC_API_KEY or OPENROUTER_API_KEY for live answers\n");
        printf("(optionally AI_MODEL to pick a model). Opening your browser instead:\n  %s\n", prompt);
        char cmd[MAX_PATH + 32];
        snprintf(cmd, sizeof(cmd), "open \"https://claude.ai/\"");
        system(cmd);
        return;
    }
    if (ok == 0) {
        printf("(No response received. Check your API key, AI_MODEL, and network connection.)\n");
        return;
    }

    printf("\nAI: %s\n", buf);
    persist_ai(hist_path, prompt, buf);
}

// Translate a natural-language request into a single my_shell command.
static int ai_translate(const char* sentence, char* out, size_t outsize) {
    const char* sys =
        "You control a custom macOS shell called my_shell. Translate the user's "
        "request into exactly ONE shell command line using only these commands: "
        "cd <dir>, pwd, echo <text>, shortcut <name> <cmd>, run <name>, "
        "list shortcuts, remove shortcut <name>, find <pattern> [path], "
        "search <ext> [path], recent <dir> [hours], screenshot [-i|-w], "
        "youtube <query>, leetcode [topic], spotify [play|pause|next|prev|search <q>], "
        "ai <question>, open <file>. "
        "Reply with ONLY the command line and nothing else. If it cannot be expressed "
        "with these commands, reply with the single word NONE.";
    int provider = (cfg_ai_provider != AI_AUTO) ? cfg_ai_provider : AI_AUTO;
    return ai_complete(sys, sentence, NULL, provider, out, outsize);
}

int command_claude(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        // Bare invocation: open Claude in the browser instead of erroring.
        printf("Opening Claude in your browser…\n");
        char* oa[] = { (char*)"open", (char*)"https://claude.ai/", NULL };
        return command_open(oa, env);
    }
    char* prompt = join_args(args, 1);
    if (!prompt) return 1;
    chat_provider(prompt, AI_ANTHROPIC, ".myshell_claude_history", "Claude", "https://claude.ai/");
    free(prompt);
    return 0;
}

int command_gpt(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        printf("Opening ChatGPT in your browser…\n");
        char* oa[] = { (char*)"open", (char*)"https://chat.openai.com/", NULL };
        return command_open(oa, env);
    }
    char* prompt = join_args(args, 1);
    if (!prompt) return 1;
    chat_provider(prompt, AI_OPENAI, ".myshell_gpt_history", "GPT", "https://chat.openai.com/");
    free(prompt);
    return 0;
}

int command_ai(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        // Bare invocation opens the configured provider's site.
        const char* url = "https://claude.ai/";
        if (cfg_ai_provider == AI_OPENAI) {
            url = "https://chat.openai.com/";
        } else if (cfg_ai_provider == AI_ANTHROPIC) {
            url = "https://claude.ai/";
        }
        printf("Opening your AI provider in the browser…\n");
        char* oa[] = { (char*)"open", (char*)url, NULL };
        return command_open(oa, env);
    }
    char* prompt = join_args(args, 1);
    if (!prompt) {
        return 1;
    }
    ask_ai(prompt);
    free(prompt);
    return 0;
}

// ---------------------------------------------------------------------------
// Natural-language dispatcher:  do <sentence>
// ---------------------------------------------------------------------------

int command_do(char** args, char** env) {
    if (args[1] == NULL) {
        printf("Usage: do <sentence>\n");
        printf("Examples:\n");
        printf("  do take a screenshot\n");
        printf("  do search youtube for cats\n");
        printf("  do open a random leetcode problem\n");
        printf("  do play some music / do pause music / do next song\n");
        printf("  do find my resume file\n");
        printf("  do ask ai what is a shell\n");
        return 1;
    }

    char* sentence = join_args(args, 1);
    if (!sentence) {
        return 1;
    }

    // Real NLP when an API key is available.
    const char* ak = getenv("ANTHROPIC_API_KEY");
    const char* ok = getenv("OPENROUTER_API_KEY");
    if (ak || ok) {
        char cmd[1024];
        if (ai_translate(sentence, cmd, sizeof(cmd)) == 1) {
            cmd[strcspn(cmd, "\n")] = '\0';
            char* s = cmd;
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '`') s++;
            char* e = s + my_strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r' || e[-1] == '`' || e[-1] == '.')) e--;
            *e = '\0';
            if (my_strcmp(s, "NONE") != 0 && s[0]) {
                printf("Interpreting: %s\n", s);
                run_shell_line(s, &env);
                free(sentence);
                return 0;
            }
        }
        printf("(AI couldn't interpret that; using keyword matching.)\n");
    }

    // Keyword fallback (no API key, or AI returned NONE).
    for (char* p = sentence; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32; // lowercase for matching
    }

    if (strstr(sentence, "screenshot") || strstr(sentence, "screen shot")) {
        const char* mode = NULL;
        if (strstr(sentence, "window")) mode = "-w";
        else if (strstr(sentence, "select") || strstr(sentence, "region") || strstr(sentence, "area")) mode = "-i";
        take_screenshot(mode);
    } else if (strstr(sentence, "youtube")) {
        char* q = strstr(sentence, " for ");
        open_youtube(q ? q + 6 : "");
    } else if (strstr(sentence, "leetcode")) {
        open_leetcode(NULL);
    } else if (strstr(sentence, "music") || strstr(sentence, "song") || strstr(sentence, "spotify")) {
        if (strstr(sentence, "pause")) spotify_control("pause", NULL);
        else if (strstr(sentence, "next")) spotify_control("next", NULL);
        else if (strstr(sentence, "prev")) spotify_control("prev", NULL);
        else if (strstr(sentence, "search") || strstr(sentence, "play")) {
            char* q = strstr(sentence, "play ");
            spotify_control("search", q ? q + 5 : sentence);
        } else {
            spotify_control("play", NULL);
        }
    } else if (strstr(sentence, "find") || strstr(sentence, "search file") || strstr(sentence, "search for")) {
        char* q = strstr(sentence, " for ");
        if (!q) q = strstr(sentence, "find ");
        if (q) q += (my_strncmp(q, " for ", 5) == 0) ? 5 : 4;
        while (*q == ' ') q++;
        search_files_by_pattern(q, ".");
    } else if (strstr(sentence, "ai") || strstr(sentence, "ask") || strstr(sentence, "chat")) {
        char* q = strstr(sentence, "ask ");
        if (!q) q = strstr(sentence, "ai ");
        if (!q) q = strstr(sentence, "chat ");
        ask_ai(q ? q + 4 : sentence);
    } else {
        printf("Sorry, I don't know how to: %s\n", sentence);
        printf("Try phrases about: screenshot, youtube, leetcode, spotify, find, ai\n");
    }

    free(sentence);
    return 0;
}
