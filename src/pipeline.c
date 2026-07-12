#include "my_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <glob.h>
#include <sys/wait.h>
#include <errno.h>

// ---------------------------------------------------------------------------
// Background job control
// ---------------------------------------------------------------------------

typedef struct {
    pid_t pid;
    char* cmd;
    int done;
    int status;
} Job;

static Job g_jobs[64];
static int g_job_count = 0;

static void add_job(pid_t pid, const char* cmd) {
    if (g_job_count >= 64) return;
    g_jobs[g_job_count].pid = pid;
    g_jobs[g_job_count].cmd = my_strdup(cmd);
    g_jobs[g_job_count].done = 0;
    g_jobs[g_job_count].status = 0;
    g_job_count++;
}

// Reap any finished background jobs so they don't linger as zombies.
void reap_background_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < g_job_count; i++) {
            if (g_jobs[i].pid == pid) {
                g_jobs[i].done = 1;
                g_jobs[i].status = status;
            }
        }
    }
}

int command_jobs(char** args, char** env) {
    (void)args;
    (void)env;
    reap_background_jobs();
    if (g_job_count == 0) {
        printf("No background jobs.\n");
        return 0;
    }
    for (int i = 0; i < g_job_count; i++) {
        if (!g_jobs[i].done) {
            printf("[%d] running  %s\n", i + 1, g_jobs[i].cmd);
        } else {
            int code = WIFEXITED(g_jobs[i].status) ? WEXITSTATUS(g_jobs[i].status) : -1;
            printf("[%d] done (exit %d)  %s\n", i + 1, code, g_jobs[i].cmd);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Ctrl-C handling
// ---------------------------------------------------------------------------

static volatile sig_atomic_t g_sigint = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_sigint = 1;
}

void init_job_control(void) {
    // Catch Ctrl-C so the shell survives it; foreground children (which run
    // with the default disposition after execve) are killed as expected.
    signal(SIGINT, on_sigint);
}

// ---------------------------------------------------------------------------
// Expansion: ~ (home) and glob (* ? []) — only for unquoted words
// ---------------------------------------------------------------------------

static int contains_glob(const char* s) {
    for (; *s; s++) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
    }
    return 0;
}

// Expand a leading ~ (or ~/...) to the user's home directory.
static char* expand_tilde(const char* w) {
    const char* rest = w + 1;
    char* home = getenv("HOME");
    if (!home) return my_strdup(w);
    size_t need = my_strlen(home) + my_strlen(rest) + 2;
    char* out = malloc(need);
    if (!out) return my_strdup(w);
    if (rest[0] == '/' || rest[0] == '\0') {
        snprintf(out, need, "%s%s", home, rest);
    } else {
        snprintf(out, need, "%s/%s", home, rest);
    }
    return out;
}

static void argv_push(char*** argv, int* n, int* cap, char* s) {
    if (*n + 1 >= *cap) {
        *cap *= 2;
        char** g = realloc(*argv, (*cap) * sizeof(char*));
        if (!g) return;
        *argv = g;
    }
    (*argv)[(*n)++] = s;
}

// Expand one word (tilde + glob) and append the result(s) to argv.
static void expand_and_push(char*** argv, int* n, int* cap, const char* word, int quoted) {
    if (quoted) {
        argv_push(argv, n, cap, my_strdup(word));
        return;
    }

    char* base = (char*)word;
    char* allocd = NULL;
    if (word[0] == '~') {
        allocd = expand_tilde(word);
        base = allocd;
    }

    if (contains_glob(base)) {
        glob_t g;
        int r = glob(base, GLOB_NOCHECK, NULL, &g);
        if (r == 0) {
            for (size_t k = 0; k < g.gl_pathc; k++) {
                argv_push(argv, n, cap, my_strdup(g.gl_pathv[k]));
            }
            globfree(&g);
        } else {
            argv_push(argv, n, cap, my_strdup(base));
        }
    } else {
        argv_push(argv, n, cap, my_strdup(base));
    }

    if (allocd) free(allocd);
}

// ---------------------------------------------------------------------------
// A single pipeline segment after parsing.
typedef struct {
    char** argv;
    char* infile;      // < file
    char* outfile;     // > / > file
    int   append;      // >> vs >
    char* errfile;     // 2> / 2>> file
    int   err_append;
    int   dup_from;    // N>&M source fd (-1 if none)
    int   dup_to;      // N>&M target fd
    int   saw_bg;
} Segment;

// True if `w` is an fd-redirection word like "2>", "2>>", "2>&1", "1>&2".
static int is_fd_redir(const char* w) {
    if (my_strlen(w) < 2) return 0;
    if (w[0] < '0' || w[0] > '9') return 0;
    return (w[1] == '>');
}

// Parse an fd-redirection word into the segment.
static void parse_fd_redir(const char* w, Segment* seg) {
    int fd = w[0] - '0';
    size_t i = 1;
    int append = 0;
    if (w[1] == '>' && w[2] == '>') { append = 1; i = 3; }

    if (w[i] == '&') {
        int target = (w[i + 1] >= '0' && w[i + 1] <= '9') ? w[i + 1] - '0' : -1;
        seg->dup_from = target;
        seg->dup_to = fd;
    } else {
        const char* rest = w + i;
        char* fn = (rest[0] == '~') ? expand_tilde(rest) : my_strdup(rest);
        if (fd == 0) { free(seg->infile); seg->infile = fn; }
        else if (fd == 1) { free(seg->outfile); seg->outfile = fn; seg->append = append; }
        else if (fd == 2) { free(seg->errfile); seg->errfile = fn; seg->err_append = append; }
    }
}

// Tokenizer: split a pipeline segment into argv + redirections, honoring quotes
// ---------------------------------------------------------------------------

// Tokenize one segment into an expanded argv plus redirection info.
static void tokenize_segment(const char* seg, Segment* out) {
    out->argv = NULL;
    out->infile = out->outfile = out->errfile = NULL;
    out->append = out->err_append = 0;
    out->dup_from = -1;
    out->dup_to = -1;
    out->saw_bg = 0;

    char* texts[1024];
    int quoted[1024];
    int isop[1024];
    int nt = 0;

    size_t i = 0, len = my_strlen(seg);
    while (i < len && nt < 1024) {
        while (i < len && (seg[i] == ' ' || seg[i] == '\t')) i++;
        if (i >= len) break;
        char c = seg[i];

        if (c == '>' || c == '<') {
            char op[3];
            if (c == '>' && i + 1 < len && seg[i + 1] == '>') {
                op[0] = '>'; op[1] = '>'; op[2] = '\0';
                i += 2;
            } else {
                op[0] = c; op[1] = '\0';
                i++;
            }
            texts[nt] = my_strdup(op);
            quoted[nt] = 0;
            isop[nt] = 1;
            nt++;
            continue;
        }

        int q = 0;
        char buf[2048];
        size_t bl = 0;
        if (c == '"' || c == '\'') {
            char qq = c;
            q = 1;
            i++;
            while (i < len && seg[i] != qq) {
                if (bl + 1 < sizeof(buf)) buf[bl++] = seg[i];
                i++;
            }
            if (i < len) i++;
        } else {
            while (i < len && seg[i] != ' ' && seg[i] != '\t') {
                if (bl + 1 < sizeof(buf)) buf[bl++] = seg[i];
                i++;
            }
        }
        buf[bl] = '\0';

        if (!q && is_fd_redir(buf)) {
            parse_fd_redir(buf, out);
            continue;
        }
        if (!q && my_strcmp(buf, "&") == 0) {
            out->saw_bg = 1;
            continue;
        }
        texts[nt] = my_strdup(buf);
        quoted[nt] = q;
        isop[nt] = 0;
        nt++;
    }

    int cap = 16;
    char** argv = malloc(cap * sizeof(char*));
    int n = 0;

    for (int k = 0; k < nt; k++) {
        if (isop[k]) {
            if (k + 1 < nt) {
                char* fn = texts[k + 1];
                char* resolved = (fn[0] == '~') ? expand_tilde(fn) : my_strdup(fn);
                if (my_strcmp(texts[k], "<") == 0) {
                    free(out->infile);
                    out->infile = resolved;
                } else {
                    free(out->outfile);
                    out->outfile = resolved;
                    out->append = (my_strcmp(texts[k], ">>") == 0);
                }
                k++;
            }
            continue;
        }
        expand_and_push(&argv, &n, &cap, texts[k], quoted[k]);
    }
    argv[n] = NULL;
    out->argv = argv;

    for (int k = 0; k < nt; k++) free(texts[k]);
}

// Split a line into raw segment strings by top-level '|' (quote-aware).
static int split_pipeline(const char* line, char*** segs_out) {
    int cap = 8;
    char** segs = malloc(cap * sizeof(char*));
    int n = 0;
    size_t i = 0, len = my_strlen(line);
    int in_q = 0;
    char q = 0;
    size_t start = 0;

    while (i <= len) {
        char c = (i < len) ? line[i] : '\0';
        if (in_q) {
            if (c == q) in_q = 0;
            i++;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_q = 1;
            q = c;
            i++;
            continue;
        }
        if (c == '|' || c == '\0') {
            size_t end = i;
            while (start < end && (line[start] == ' ' || line[start] == '\t')) start++;
            while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
            size_t sl = end - start;
            char* s = malloc(sl + 1);
            my_strncpy(s, line + start, sl);
            s[sl] = '\0';
            if (n + 1 >= cap) {
                cap *= 2;
                segs = realloc(segs, cap * sizeof(char*));
            }
            segs[n++] = s;
            start = i + 1;
        }
        i++;
    }
    segs[n] = NULL;
    *segs_out = segs;
    return n;
}

int is_builtin(const char* name) {
    const char* b[] = {"cd", "pwd", "echo", "env", "setenv", "unsetenv", "which",
                       "shortcut", "run", "list", "remove", "find", "search", "recent",
                       "screenshot", "youtube", "leetcode", "spotify", "ai", "claude",
                       "gpt", "do", "clip", "open", "weather", "note", "git",
                       "battery", "wifi", "timer", ".help", "jobs", NULL};
    for (int i = 0; b[i]; i++) {
        if (my_strcmp(name, b[i]) == 0) return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Pipeline execution
// ---------------------------------------------------------------------------

// In a forked pipeline stage: apply redirections, then run built-in (in child)
// or exec the external command.
static void exec_stage_child(char** argv, char** env, char* initial_dir) {
    if (argv[0] == NULL) _exit(0);
    if (is_builtin(argv[0])) {
        shell_builts(argv, env, initial_dir);
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
    char* path = resolve_exec(argv[0], env);
    if (!path) {
        fprintf(stderr, "my_shell: command not found: %s\n", argv[0]);
        fflush(stderr);
        _exit(127);
    }
    execve(path, argv, env);
    perror("execve");
    _exit(127);
}

static void wait_for_child(pid_t pid) {
    int status;
    while (1) {
        pid_t r = waitpid(pid, &status, 0);
        if (r == -1) {
            if (errno == EINTR) {
                if (g_sigint) {
                    g_sigint = 0;
                    printf("\n");
                    int st;
                    waitpid(pid, &st, 0); // reap the killed child
                    return;
                }
                continue;
            }
            return;
        }
        if (WIFSIGNALED(status) && WTERMSIG(status) != SIGINT) {
            fprintf(stderr, "Terminated by signal %d\n", WTERMSIG(status));
        }
        return;
    }
}

static int run_pipeline(int nseg, Segment* segs, int background, char** env,
                        char* initial_dir, const char* raw) {
    int prev_read = -1;
    pid_t* pids = malloc((nseg > 0 ? nseg : 1) * sizeof(pid_t));

    for (int s = 0; s < nseg; s++) {
        int pipefd[2] = { -1, -1 };
        int is_last = (s == nseg - 1);
        if (!is_last) {
            if (pipe(pipefd) != 0) { perror("pipe"); break; }
        }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); break; }

        if (pid == 0) {
            if (background) setpgid(0, 0); // detach from the terminal's pgrp
            Segment* seg = &segs[s];

            if (seg->infile) {
                int fd = open(seg->infile, O_RDONLY);
                if (fd < 0) { perror(seg->infile); _exit(1); }
                dup2(fd, 0);
                close(fd);
            } else if (prev_read != -1) {
                dup2(prev_read, 0);
            }

            if (seg->outfile) {
                int flags = O_WRONLY | O_CREAT | (seg->append ? O_APPEND : O_TRUNC);
                int fd = open(seg->outfile, flags, 0644);
                if (fd < 0) { perror(seg->outfile); _exit(1); }
                dup2(fd, 1);
                close(fd);
            } else if (!is_last) {
                dup2(pipefd[1], 1);
            }

            if (seg->errfile) {
                int flags = O_WRONLY | O_CREAT | (seg->err_append ? O_APPEND : O_TRUNC);
                int fd = open(seg->errfile, flags, 0644);
                if (fd < 0) { perror(seg->errfile); _exit(1); }
                dup2(fd, 2);
                close(fd);
            } else if (seg->dup_from >= 0) {
                dup2(seg->dup_from, seg->dup_to);
            }

            if (prev_read != -1) close(prev_read);
            if (!is_last) { close(pipefd[0]); close(pipefd[1]); }

            if (seg->argv[0] == NULL) _exit(0);
            exec_stage_child(seg->argv, env, initial_dir);
            _exit(127);
        }

        if (prev_read != -1) close(prev_read);
        if (!is_last) { close(pipefd[1]); prev_read = pipefd[0]; }
        pids[s] = pid;
    }

    if (background) {
        add_job(pids[nseg - 1], raw);
        printf("[%d] %d\n", g_job_count, pids[nseg - 1]);
    } else {
        for (int s = 0; s < nseg; s++) wait_for_child(pids[s]);
    }

    free(pids);
    return 0;
}

// ---------------------------------------------------------------------------
// Split a line into separate commands by top-level '&' (quote-aware). Each '&'
// backgrounds the command that immediately precedes it.
// ---------------------------------------------------------------------------

static int split_by_and(const char* line, char*** cmds_out, int** bg_out) {
    int cap = 8;
    char** cmds = malloc(cap * sizeof(char*));
    int* bg = malloc(cap * sizeof(int));
    int n = 0;

    size_t i = 0, len = my_strlen(line);
    int in_q = 0;
    char q = 0;
    size_t start = 0;
    int cur_bg = 0;

    while (i <= len) {
        char c = (i < len) ? line[i] : '\0';
        if (in_q) {
            if (c == q) in_q = 0;
            i++;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_q = 1;
            q = c;
            i++;
            continue;
        }
        // Background '&' only when it's a separate token (not part of "2>&1").
        int is_bg_sep = (c == '&') && !(i > 0 && line[i - 1] == '>');
        if (is_bg_sep || c == '\0') {
            size_t end = i;
            while (start < end && (line[start] == ' ' || line[start] == '\t')) start++;
            while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
            size_t sl = end - start;
            if (sl > 0) {
                char* s = malloc(sl + 1);
                my_strncpy(s, line + start, sl);
                s[sl] = '\0';
                if (n + 1 >= cap) {
                    cap *= 2;
                    cmds = realloc(cmds, cap * sizeof(char*));
                    bg = realloc(bg, cap * sizeof(int));
                }
                cmds[n] = s;
                bg[n] = (c == '&') ? 1 : cur_bg; // '&' backgrounds the cmd before it
                n++;
            }
            if (c == '&') cur_bg = 0;
            start = i + 1;
        }
        i++;
    }

    cmds[n] = NULL;
    *cmds_out = cmds;
    *bg_out = bg;
    return n;
}

// ---------------------------------------------------------------------------
// Run one command (which may itself be a pipeline).
// ---------------------------------------------------------------------------

static int run_one_command(const char* cmd, int background, char** env, char* initial_dir) {
    char** segs_str = NULL;
    int nseg = split_pipeline(cmd, &segs_str);
    if (nseg == 0) {
        free(segs_str);
        return 0;
    }

    Segment* segs = malloc(nseg * sizeof(Segment));
    for (int s = 0; s < nseg; s++) {
        tokenize_segment(segs_str[s], &segs[s]);
    }

    int rc = 0;
    if (nseg == 1 && !segs[0].infile && !segs[0].outfile && !segs[0].errfile &&
        segs[0].dup_from < 0 && !background) {
        // Single, unredirected command: run built-ins in the shell so that
        // cd / setenv / etc. affect the session; external commands fork.
        rc = shell_builts(segs[0].argv, env, initial_dir);
    } else {
        run_pipeline(nseg, segs, background, env, initial_dir, cmd);
    }

    for (int s = 0; s < nseg; s++) {
        if (segs[s].argv) {
            for (int j = 0; segs[s].argv[j]; j++) free(segs[s].argv[j]);
            free(segs[s].argv);
        }
        free(segs[s].infile);
        free(segs[s].outfile);
        free(segs[s].errfile);
        free(segs_str[s]);
    }
    free(segs);
    free(segs_str);
    return rc;
}

// ---------------------------------------------------------------------------
// Entry point: split into '&'-separated commands, run each.
// ---------------------------------------------------------------------------

int dispatch_line(const char* line, char** env, char* initial_dir) {
    g_sigint = 0;

    char** cmds = NULL;
    int* bg = NULL;
    int n = split_by_and(line, &cmds, &bg);

    for (int i = 0; i < n; i++) {
        run_one_command(cmds[i], bg[i], env, initial_dir);
    }

    for (int i = 0; i < n; i++) free(cmds[i]);
    free(cmds);
    free(bg);
    return 0;
}
