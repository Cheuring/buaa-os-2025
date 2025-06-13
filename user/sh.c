#include <args.h>
#include <history.h>
#include <lib.h>
#include <variable.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"
#define PROMPT "mos> "
#define DEL 0x7f
#define ESC 0x1b
#define BACKSPACE 0x08

static struct History history;
static struct VariableSet variable_set;
static char cwd[MAXPATHLEN];
static int interactive;
static int storedFd[2];

void runcmd(char *);
int _declare(int, char **);
int _unset(int, char **);
int _cd(int, char **);
int _pwd(int, char **);
int _history(int, char **);
int _exit(int, char **);

struct BuiltinCmd {
    const char *name;
    int (*func)(int, char **);
};

static struct BuiltinCmd builtin_cmds[] = {
    {"cd", _cd},
    {"pwd", _pwd},
    {"history", _history},
    {"declare", _declare},
    {"unset", _unset},
    {"exit", _exit},
    {0, 0}  // Sentinel
};

#define PRINTF(...)              \
    do {                         \
        if (interactive) {       \
            printf(__VA_ARGS__); \
        }                        \
    } while (0)

#define PUT_CHAR(c)                   \
    do {                              \
        if (!interactive) {           \
            break;                    \
        }                             \
        if ((c) < 32 || (c) >= 127) { \
            printf("?");              \
        } else {                      \
            printf("%c", (c));        \
        }                             \
    } while (0)

/* Overview:
 *   Parse the next token from the string at s.
 *
 * Post-Condition:
 *   Set '*p1' to the beginning of the token and '*p2' to just past the token.
 *   Return:
 *     - 0 if the end of string is reached.
 *     - '<' for < (stdin redirection).
 *     - '>' for > (stdout redirection).
 *     - '|' for | (pipe).
 *     - 'w' for a word (command, argument, or file name).
 *     - 'a' for >> (append redirection).
 *     - 'A' for && (logical AND).
 *     - 'O' for || (logical OR).
 *
 *   The buffer is modified to turn the spaces after words into zero bytes
 * ('\0'), so that the returned token is a null-terminated string.
 */
int _gettoken(char *s, char **p1, char **p2) {
    static const struct {
        char c1;
        char c2;
        int ret_val;
    } double_char_tokens[] = {
        {'>', '>', 'a'}, {'&', '&', 'A'}, {'|', '|', 'O'}, {0, 0, 0}};

    *p1 = 0;
    *p2 = 0;
    if (s == 0) {
        return 0;
    }

    while (strchr(WHITESPACE, *s)) {
        *s++ = 0;
    }
    if (*s == 0) {
        return 0;
    }

    for (int i = 0; double_char_tokens[i].c1 != 0; ++i) {
        if (*s == double_char_tokens[i].c1 &&
            *(s + 1) == double_char_tokens[i].c2) {
            *p1 = s;
            *s++ = 0;
            *s++ = 0;
            *p2 = s;
            return double_char_tokens[i].ret_val;
        }
    }

    if (strchr(SYMBOLS, *s)) {
        int t = *s;
        *p1 = s;
        *s++ = 0;
        *p2 = s;
        return t;
    }

    *p1 = s;
    while (*s && !strchr(WHITESPACE SYMBOLS, *s)) {
        s++;
    }
    *p2 = s;
    return 'w';
}

int gettoken(char *s, char **p1) {
    static int c, nc;
    static char *np1, *np2;

    if (s) {
        nc = _gettoken(s, &np1, &np2);
        return 0;
    }
    c = nc;
    *p1 = np1;
    nc = _gettoken(np2, &np1, &np2);
    return c;
}

#define MAXARGS 128

int isDir(const char *path) {
    struct Stat st;
    int r;
    if ((r = stat(path, &st)) < 0) {
        return r;
    }

    return st.st_isdir;
}

int fork1(void) {
    int r;
    if ((r = fork()) < 0) {
        debugf("fork: %d\n", r);
        exit(1);
    }
    DEBUGF("fork1: %d\n", r);
    return r;
}

static void store_01(int p[2]) {
    int r;
    if ((r = pipe(p)) < 0) {
        user_panic("pipe: %d", r);
    }
    if ((r = dup(0, p[0])) < 0) {
        user_panic("dup store: %d", r);
    }
    if ((r = dup(1, p[1])) < 0) {
        user_panic("dup store: %d", r);
    }
}
static void restore_01(int p[2]) {
    int r;
    if ((r = dup(p[0], 0)) < 0) {
        user_panic("dup restore: %d", r);
    }
    if ((r = dup(p[1], 1)) < 0) {
        user_panic("dup restore: %d", r);
    }
}

char *process_backticks(char *cmd) {
    char *result = cmd;
    char *start = (char *)strchr(cmd, '`');
    if (!start) {
        return result;
    }

    char new_cmd[MAX_COMMAND_LENGTH] = {0};
    int new_cmd_len = 0;

    // copy the part before the first backtick
    strncpy(new_cmd, cmd, start - cmd);
    new_cmd_len = start - cmd;

    while (start) {
        char *end = (char *)strchr(start + 1, '`');
        if (!end) {
            fprintf(2, "Error: Unmatched backtick\n");
            *cmd = 0;
            return cmd;
        }
        *end = 0;

        int p[2];
        if (pipe(p) < 0) {
            fprintf(2, "process_backticks: Failed to create pipe\n");
            exit(1);
        }

        int child = fork1();
        if (child == 0) {
            close(p[0]);
            if (dup(p[1], 1) < 0) {
                fprintf(
                    2,
                    "process_backticks: Failed to duplicate pipe write end\n");
                close(p[1]);
                exit(1);
            }
            close(p[1]);
            runcmd(start + 1);
            exit(0);
        }

        close(p[1]);

        char output[MAX_COMMAND_LENGTH] = {0};
        int output_len = 0;

        char ch;
        while (output_len < MAX_COMMAND_LENGTH - 1 && read(p[0], &ch, 1) == 1) {
            // remove newlines from the output
            if (ch == '\n') {
                ch = ' ';
            }
            output[output_len++] = ch;
        }
        close(p[0]);
        wait(child);

        if (output_len >= MAX_COMMAND_LENGTH - 1) {
            goto err;
        }
        output[output_len] = 0;

        // append the output to the new command
        for (int i = 0; i < output_len; i++) {
            if (new_cmd_len + 1 < MAX_COMMAND_LENGTH) {
                new_cmd[new_cmd_len++] = output[i];
            } else {
                goto err;
            }
        }

        start = (char *)strchr(end + 1, '`');
        char *remainder = end + 1;
        int len_to_copy = start ? (start - end - 1) : strlen(remainder);

        for (int i = 0; i < len_to_copy; i++) {
            if (new_cmd_len + 1 < MAX_COMMAND_LENGTH) {
                new_cmd[new_cmd_len++] = remainder[i];
            } else {
                goto err;
            }
        }

        restore_01(storedFd);
    }

    new_cmd[new_cmd_len] = 0;
    strcpy(cmd, new_cmd);
    return cmd;

err:
    fprintf(2, "Error: Command too long\n");
    *cmd = 0;  // clear the command
    restore_01(storedFd);
    return cmd;
}

int parsecmd(char **argv, int *rightpipe, int *isChild) {
    int argc = 0;
    char *t;
    int fd, r, c;

    while (1) {
        c = gettoken(0, &t);
        switch (c) {
            case 0:
                return argc;
            case 'w':
                if (argc >= MAXARGS) {
                    fprintf(2, "too many arguments\n");
                    exit(1);
                }
                argv[argc++] = t;
                break;
            case '<':
                if (gettoken(0, &t) != 'w') {
                    fprintf(2, "syntax error: < not followed by word\n");
                    exit(1);
                }

                r = isDir(t);
                if (r == 1) {
                    fprintf(2, "bash: %s: Is a directory\n", t);
                    return 0;
                }

                if ((fd = open(t, O_RDONLY)) < 0) {
                    fprintf(2, "failed to open '%s'\n", t);
                    exit(1);
                }
                dup(fd, 0);
                close(fd);

                break;
            case '>':
            case 'a':;
                int mode = c == '>' ? O_TRUNC : O_APPEND;
                if (gettoken(0, &t) != 'w') {
                    fprintf(2, "syntax error: %s not followed by word\n",
                            c == '>' ? ">" : ">>");
                    exit(1);
                    // return 0;
                }

                r = isDir(t);
                if (r == 1) {
                    fprintf(2, "bash: %s: Is a directory\n", t);
                    return 0;
                }

                if ((fd = open(t, O_WRONLY | O_CREAT | mode)) < 0) {
                    fprintf(2, "failed to open '%s'\n", t);
                    exit(1);
                }
                dup(fd, 1);
                close(fd);

                break;
            case '|':;
                int p[2];
                if ((r = pipe(p)) != 0) {
                    fprintf(2, "pipe: %d\n", r);
                    exit(1);
                }
                r = fork1();
                *rightpipe = r;
                if (r == 0) {
                    dup(p[0], 0);
                    close(p[0]);
                    close(p[1]);
                    *isChild = 1;
                    return parsecmd(argv, rightpipe, isChild);
                } else {
                    dup(p[1], 1);
                    close(p[1]);
                    close(p[0]);
                    return argc;
                }

                break;
            case ';':
                r = fork1();
                if (r) {
                    wait(r);
                    restore_01(storedFd);
                    return parsecmd(argv, rightpipe, isChild);
                }
                *isChild = 1;

                return argc;
            case 'A':
            case 'O':;
                // logical AND or OR
                int child = fork1();
                if (child == 0) {
                    // child process
                    *isChild = 1;
                    return argc;
                }

                r = wait(child);
                DEBUGF("wait %d: %d\n", child, r);
                if ((c == 'A' && r == 0) || (c == 'O' && r != 0)) {
                    return parsecmd(argv, rightpipe, isChild);
                }

                return 0;
        }
    }

    return argc;
}

void runcmd(char *s) {
    gettoken(s, 0);

    char *argv[MAXARGS];
    int rightpipe = 0, isChild = 0;

    int argc = parsecmd(argv, &rightpipe, &isChild);
    if (argc == 0) {
        return;
    }
    argv[argc] = 0;

    int r;
    // todo: exit if is child
    for (int i = 0; builtin_cmds[i].name; i++) {
        if (strcmp(argv[0], builtin_cmds[i].name) == 0) {
            r = builtin_cmds[i].func(argc, argv);
            goto out;
        }
    }

    int child = spawn(argv[0], argv);
    if (child >= 0) {
        DEBUGF("spawn %s: %d\n", argv[0], child);
        r = wait(child);
    } else {
        fprintf(2, "spawn %s: %d\n", argv[0], child);
    }

out:
    if (rightpipe) {
        close(1);  // close write end of pipe
        // only if both side of the pipe exit successfully
        // the exit status will be 0
        r |= wait(rightpipe);
    }
    if (isChild) {
        // child process, exit
        exit(r);
    }
}

void readline(char *buf, u_int n) {
    int r, i = 0;
    char c;
    static char backbuf[1024];
    int backbuf_i = 0;
    static enum { NORMAL, GOT_ESC, GOT_BRACKET } state = NORMAL;

    while (i + backbuf_i < n - 1) {
        if ((r = read(0, &c, 1)) != 1) {
            if (r < 0) {
                fprintf(2, "read error: %d\n", r);
            }
            exit(1);
        }

    back:
        if (state == NORMAL) {
            switch (c) {
                case BACKSPACE:
                case DEL:
                    // delete
                    if (i > 0) {
                        i--;
                        if (interactive) {
                            printf("\b");
                            for (int k = backbuf_i - 1; k >= 0; k--) {
                                PUT_CHAR(backbuf[k]);
                            }
                            printf(" \033[%dD", backbuf_i + 1);
                        }
                    }
                    break;

                case '\r':
                case '\n':
                    // new line
                    PRINTF("\n");
                    for (int k = backbuf_i - 1; k >= 0; k--) {
                        buf[i++] = backbuf[k];
                    }
                    buf[i] = 0;
                    return;

                case ESC:
                    state = GOT_ESC;
                    break;

                case C('E'):
                    // move cursor to the end
                    if (backbuf_i > 0) {
                        PRINTF("\033[%dC", backbuf_i);
                        for (int k = backbuf_i - 1; k >= 0; k--) {
                            buf[i++] = backbuf[k];
                        }
                        backbuf_i = 0;
                    }
                    break;

                case C('A'):
                    // move cursor to the start
                    if (i > 0) {
                        PRINTF("\033[%dD", i);
                        for (int k = i - 1; k >= 0; --k) {
                            backbuf[backbuf_i++] = buf[k];
                        }
                        i = 0;
                    }
                    break;

                case C('K'):
                    // clear line from cursor to end
                    PRINTF("\033[K");
                    backbuf_i = 0;
                    break;

                case C('U'):
                    // clear line from start to cursor
                    if (i > 0) {
                        if (interactive) {
                            printf("\r\033[K%s", PROMPT);
                            for (int k = backbuf_i - 1; k >= 0; k--) {
                                PUT_CHAR(backbuf[k]);
                            }
                            if (backbuf_i > 0) {
                                printf("\033[%dD", backbuf_i);
                            }
                        }
                        i = 0;
                    }
                    break;

                case C('W'):
                    // delete word
                    {
                        int temp_i = i;
                        while (temp_i > 0 &&
                               strchr(WHITESPACE, buf[temp_i - 1])) {
                            temp_i--;
                        }
                        while (temp_i > 0 &&
                               !strchr(WHITESPACE, buf[temp_i - 1])) {
                            temp_i--;
                        }

                        if (i > temp_i) {
                            if (interactive) {
                                // move cursor
                                printf("\033[%dD", i - temp_i);
                                // clear till end of line
                                printf("\033[K");
                                // redraw the back buffer
                                for (int k = backbuf_i - 1; k >= 0; k--) {
                                    PUT_CHAR(backbuf[k]);
                                }
                                // remove the word from the buffer
                                if (backbuf_i > 0) {
                                    printf("\033[%dD", backbuf_i);
                                }
                            }

                            i = temp_i;
                        }
                    }
                    break;

                case C('P'):
                    break;

                default:
                    buf[i++] = c;
                    if (interactive) {
                        PUT_CHAR(c);

                        for (int k = backbuf_i - 1; k >= 0; k--) {
                            PUT_CHAR(backbuf[k]);
                        }

                        if (backbuf_i > 0) {
                            printf("\033[%dD", backbuf_i);
                        }
                    }
            }
        } else if (state == GOT_ESC) {
            if (c == '[') {
                state = GOT_BRACKET;
            } else {
                state = NORMAL;
                // not a bracket, go back
                goto back;
            }
        } else {
            switch (c) {
                case 'A':
                    // up arrow
                    // stage command
                    stage_command(&history, buf, &i, backbuf, &backbuf_i);
                    // get previous command
                    move_history_cursor(&history, buf, &i, -1);
                    // print the command
                    PRINTF("\r\033[K%s%s", PROMPT, buf);
                    break;
                case 'B':
                    // down arrow
                    // stage command
                    stage_command(&history, buf, &i, backbuf, &backbuf_i);
                    // get next command
                    move_history_cursor(&history, buf, &i, 1);
                    // print the command
                    PRINTF("\r\033[K%s%s", PROMPT, buf);
                    break;
                case 'C':
                    // right arrow
                    // move cursor right
                    if (backbuf_i > 0) {
                        PRINTF("\033[C");
                        buf[i++] = backbuf[--backbuf_i];
                    }
                    break;
                case 'D':
                    // left arrow
                    // move cursor left
                    if (i > 0) {
                        PRINTF("\033[D");
                        backbuf[backbuf_i++] = buf[--i];
                    }
                    break;
                default:
                    PRINTF("[");
                    buf[i++] = '[';
                    state = NORMAL;
                    goto back;
            }
            state = NORMAL;
        }
    }
    fprintf(2, "line too long\n");
    while ((r = read(0, buf, 1)) == 1 && buf[0] != '\r' && buf[0] != '\n') {
        ;
    }
    buf[0] = 0;
}

char buf[MAX_COMMAND_LENGTH];

void usage(void) {
    printf("usage: sh [-ix] [script-file]\n");
    exit(1);
}

int main(int argc, char **argv) {
    int r;
    interactive = iscons(0);
    int echocmds = 0;
    char *comment;
    ARGBEGIN {
        case 'i':
            interactive = 1;
            break;
        case 'x':
            echocmds = 1;
            break;
        default:
            usage();
    }
    ARGEND

    if (argc > 1) {
        usage();
    }
    if (argc == 1) {
        // if a script file is given, cannot be interactive
        interactive = 0;
        close(0);
        if ((r = open(argv[0], O_RDONLY)) < 0) {
            user_panic("open %s: %d", argv[0], r);
        }
        user_assert(r == 0);
    }

    if (interactive) {
        printf(
            "\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
            "\n");
        printf(
            "::                                                         ::\n");
        printf(
            "::                     MOS Shell 2024                      ::\n");
        printf(
            "::                                                         ::\n");
        printf(
            ":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
    }

    // store original 0/1 fd
    store_01(storedFd);

    load_command_history(&history);
    init_vars(&variable_set);
    strcpy(cwd, (const char *)env->cwd_name);

    for (;;) {
        PRINTF("\n%s", PROMPT);
        readline(buf, sizeof buf);
        add_history(&history, buf);

        comment = (char *)strchr(buf, '#');
        if (comment) {
            *comment = 0;  // truncate at comment
        }
        if (echocmds) {
            printf("# %s\n", buf);
        }

        if (expand_vars(&variable_set, buf) < 0) {
            fprintf(2, "Error: Variable expansion failed\n");
            DEBUGF("expand_vars failed: %s\n", buf);
            continue;
        }
        char *cmd = process_backticks(buf);
        runcmd(cmd);
        // restore original 0/1 fd
        restore_01(storedFd);
    }
    return 0;
}

int _declare(int argc, char **argv) {
    int export_flag = 0, readonly_flag = 0;
    char *name = NULL, *value = NULL;

    ARGBEGIN {
        case 'x':
            export_flag = 1;
            break;
        case 'r':
            readonly_flag = 1;
            break;
    }
    ARGEND

    if (argc == 0) {
        print_vars(&variable_set);
        return 0;
    }

    name = argv[0];
    value = (char *)strchr(argv[0], '=');
    if (!value || name == value || !*(value + 1)) {
        // If no '=' found or name is empty or value is empty
        fprintf(2, "declare: syntax error: expected name=value\n");
        return -E_INVAL;
    }

    *value++ = 0;
    return declare_var(&variable_set, name, value, export_flag, readonly_flag);
}

int _unset(int argc, char **argv) {
    // int r;
    // if (argc == 1) {
    //     fprintf(2, "unset: expected at least one argument\n");
    //     return -E_INVAL;
    // }

    // for (int i = 1; i < argc; i++) {
    //     if ((r = unset_var(&variable_set, argv[i])) < 0) {
    //         fprintf(2, "unset: failed to unset variable '%s'\n", argv[i]);
    //         return r;
    //     }
    // }
    if (argc != 2) {
        fprintf(2, "unset: expected 1 argument; got %d\n", argc - 1);
        return -E_INVAL;
    }

    return unset_var(&variable_set, argv[1]);
}

int _cd(int argc, char **argv) {
    int r;
    switch (argc) {
        case 1:
            argv[1] = "/";
        case 2:
            if ((r = chdir(argv[1])) < 0) {
                if (r == -E_NOT_FOUND) {
                    fprintf(2, "cd: The directory '%s' does not exist\n",
                            argv[1]);
                } else if (r == -E_NOT_DIR) {
                    fprintf(2, "cd: '%s' is not a directory\n", argv[1]);
                } else {
                    fprintf(2, "cd failed %s: %d\n", argv[1], r);
                }

                return r;
            }
            strcpy(cwd, (const char *)env->cwd_name);

            break;

        default:
            fprintf(2, "Too many args for cd command\n");
            return -E_INVAL;
    }

    return 0;
}

int _pwd(int argc, char **argv) {
    if (argc > 1) {
        fprintf(2, "pwd: expected 0 arguments; got %d\n", argc - 1);
        return -E_INVAL;
    }

    printf("%s\n", cwd);
    return 0;
}

int _history(int argc, char **argv) {
    if (argc > 1) {
        fprintf(2, "history: expected 0 arguments; got %d\n", argc - 1);
        return -E_INVAL;
    }

    show_history(&history);
    return 0;
}

int _exit(int argc, char **argv) {
    if (argc > 1) {
        fprintf(2, "exit: expected 0 arguments; got %d\n", argc - 1);
        return -E_INVAL;
    }

    // save history before exiting
    save_command_history(&history);
    exit(0);
}