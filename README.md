# ShellCraft

A custom Unix shell written in C, with zsh-style conveniences and a set of
built-in productivity commands. It implements its own tokenizer, executor,
pipeline engine, and job control, plus a collection of macOS-oriented helper
commands and "fun extras."

## Build & run

Requires `gcc` and the `libedit` library (for readline-style line editing and
TAB completion).

```sh
make            # builds the ./my_shell binary
./my_shell      # start an interactive session
make clean      # remove object files
make fclean     # remove binary + object files
make re         # rebuild from scratch
```

On a fresh macOS machine you may need the command-line tools:
`xcode-select --install`.

## Shell features

- **Pipes:** `cmd1 | cmd2 | cmd3`
- **Redirection:** `> file`, `>> file`, `< file`, and `2>&1`
- **Background jobs:** append `&` to a command; inspect with `jobs`
- **Globbing:** `*.c`, `src/*.h`, etc.
- **Tilde expansion:** a leading `~` expands to `$HOME`
- **Ctrl-C** interrupts the foreground command
- **TAB completion** for builtin names and saved shortcut names
- **Command history** persisted to `~/.myshell_history`

## Built-in commands

### Navigation & environment
| Command | Description |
| --- | --- |
| `cd <dir>` | Change the current directory |
| `pwd` | Print the working directory |
| `echo <text>` | Print text (handles `"double"` and `'single'` quotes) |
| `env` | List all environment variables |
| `setenv VAR=value` | Set an environment variable |
| `unsetenv <var>` | Remove an environment variable |
| `which <command>` | Locate an executable in `$PATH` |

### Shortcuts (see below)
| Command | Description |
| --- | --- |
| `shortcut <name> <command>` | Create a named shortcut |
| `run <name>` | Run a saved shortcut |
| `list shortcuts` | List all saved shortcuts |
| `remove shortcut <name>` | Delete a shortcut |

### File search
| Command | Description |
| --- | --- |
| `find <pattern> [path]` | Find files whose name contains `<pattern>` |
| `search <ext> [path]` | Find files by extension (e.g. `search .c`) |
| `recent <dir> [hours]` | List files modified in the last N hours (default 24) |

### Tools (macOS helpers)
| Command | Description |
| --- | --- |
| `screenshot [-i\|-w]` | Capture the screen (`-i` interactive, `-w` window). Needs Screen Recording permission |
| `youtube <query>` | Search YouTube in your browser |
| `leetcode [topic]` | Open a random LeetCode problem |
| `spotify [play\|pause\|next\|prev\|search <q>]` | Control the Spotify macOS app |
| `ai [question]` | Ask an AI (uses the configured provider / API key). With no args, opens the provider's site |
| `claude [question]` | Ask Claude (needs `ANTHROPIC_API_KEY`). With no args, opens claude.ai |
| `gpt [question]` | Ask ChatGPT (needs `OPENROUTER_API_KEY`). With no args, opens chat.openai.com |
| `do <sentence>` | Run a command described in plain English |

### Fun extras
| Command | Description |
| --- | --- |
| `clip [text]` | Copy `text` to (or, with no args, paste from) the system clipboard — macOS `pbcopy`/`pbpaste` |
| `open [file]` | Open a file/folder with the default app (`open` / `xdg-open`) |
| `weather [city]` | Show current weather via wttr.in (needs `curl` + network) |
| `note [text]` | Save a timestamped note, or print all saved notes with no args |
| `git <s\|l\|a\|c\|p\|pl\|d>` | Git shortcuts: `s`tatus, `l`og, `a`dd, `c`ommit, `p`ush, `p`u`l`l, `d`iff |
| `battery` | Show battery charge/status (macOS `pmset`) |
| `wifi` | Show the current Wi-Fi network (macOS `airport`) |
| `timer <secs> [msg]` | Background countdown that prints a message when finished |

### Jobs & session
| Command | Description |
| --- | --- |
| `jobs` | List background jobs |
| `.help` | Show the in-shell help text |
| `exit` / `quit` | Exit the shell |

## Shortcuts

A shortcut stores a command line that is re-parsed and executed like any other
command, so it inherits **pipes, redirection, background (`&`), and `~`
expansion**.

```sh
shortcut brave "open -a 'Brave Browser'"   # quoted args with spaces stay together
shortcut desk  "cd ~/Desktop"
shortcut gh    "open https://github.com"
shortcut stat  "git s"
run brave                                      # launches Brave
list shortcuts                                 # show everything
remove shortcut brave                          # delete it
```

Shortcuts are saved in plain text at **`~/.myshell_shortcuts`**, one per line as
`name|command`, and are loaded automatically at startup, so they persist across
sessions.

**Limitations:**
- `run <name>` ignores anything typed after the name — bake values into the
  stored command (e.g. `git c wip` rather than expecting a message at runtime).
- A shortcut is a single command or pipeline; it can't chain separate commands
  with `;`.
- `~` expands only at the start of the command; use `$HOME` or an absolute path
  for mid-command paths.

## Notes

`note <text>` appends a line formatted `- [YYYY-MM-DD HH:MM] <text>` to
**`~/.myshell_notes.md`** in your home directory. Running `note` with no
arguments prints the whole file.

## Configuration (`~/.myshellrc`)

The shell reads `~/.myshellrc` at startup. Supported settings:

- `prompt <string>` — custom prompt
- `ai_provider auto|anthropic|openai` — default AI provider for `ai`
- `ai_model <model>` — default model for `ai`
- `screenshot_mode <mode>` — default mode for `screenshot`
- `shortcut <name> <command>` — pre-define a shortcut (skipped if the name
  already exists)

## Platform notes

Several commands are macOS-only and degrade gracefully with a message on other
systems:

- `screenshot`, `spotify` — require the macOS app + appropriate permissions
- `clip`, `battery`, `wifi` — use `pbcopy`/`pmset`/`airport`
- `open` falls back to `xdg-open` on Linux

`weather` requires network access and `curl` on any platform.
