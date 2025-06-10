#include <args.h>
#include <history.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"
#define PROMPT "mos> "
#define DEL 0x7f
#define ESC 0x1b
#define BACKSPACE 0x08

static struct History history;
static char cwd[MAXPATHLEN];
static int interactive;
static int storedFd[2];

void runcmd(char *);

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
 *
 *   The buffer is modified to turn the spaces after words into zero bytes
 * ('\0'), so that the returned token is a null-terminated string.
 */
int _gettoken(char *s, char **p1, char **p2) {
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
        exit();
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
        if(pipe(p) < 0) {
            fprintf(2, "process_backticks: Failed to create pipe\n");
            exit();
        }

        int child = fork1();
        if (child == 0) {
            close(p[0]);
            if (dup(p[1], 1) < 0) {
                fprintf(2, "process_backticks: Failed to duplicate pipe write end\n");
                close(p[1]);
                exit();
            }
            close(p[1]);
            runcmd(start + 1);
            exit();
        }

        close(p[1]);

        char output[MAX_COMMAND_LENGTH] = {0};
        int output_len = 0;

        char ch;
        while (output_len < MAX_COMMAND_LENGTH - 1 &&
                read(p[0], &ch, 1) == 1) {
            // remove newlines from the output
            if (ch == '\n') {
                ch = ' ';
            }
            output[output_len++] = ch;
        }

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
                    exit();
                }
                argv[argc++] = t;
                break;
            case '<':
                if (gettoken(0, &t) != 'w') {
                    fprintf(2, "syntax error: < not followed by word\n");
                    exit();
                }

                r = isDir(t);
                if (r == 1) {
                    fprintf(2, "bash: %s: Is a directory\n", t);
                    return 0;
                }

                if ((fd = open(t, O_RDONLY)) < 0) {
                    fprintf(2, "failed to open '%s'\n", t);
                    exit();
                }
                dup(fd, 0);
                close(fd);

                break;
            case '>':
                if (gettoken(0, &t) != 'w') {
                    fprintf(2, "syntax error: > not followed by word\n");
                    exit();
                }

                r = isDir(t);
                if (r == 1) {
                    fprintf(2, "bash: %s: Is a directory\n", t);
                    return 0;
                }

                if ((fd = open(t, O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
                    fprintf(2, "failed to open '%s'\n", t);
                    exit();
                }
                dup(fd, 1);
                close(fd);

                break;
            case '|':;
                int p[2];
                if ((r = pipe(p)) != 0) {
                    fprintf(2, "pipe: %d\n", r);
                    exit();
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

                break;
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
    if (strcmp("cd", argv[0]) == 0) {
        switch (argc) {
            case 1:
                argv[1] = "/";
            case 2:
                if ((r = chdir(argv[1])) < 0) {
                    if(r == -E_NOT_FOUND){
                        fprintf(2, "cd: The directory '%s' does not exist\n", argv[1]);
                    }else if (r == -E_NOT_DIR) {
                        fprintf(2, "cd: '%s' is not a directory\n", argv[1]);
                    } else {
                        fprintf(2, "cd failed %s: %d\n", argv[1], r);
                    }
                } else {
                    strcpy(cwd, (const char *)env->cwd_name);
                }

                break;

            default:
                fprintf(2, "Too many args for cd command\n");
        }

        return;
    }
    if (strcmp("pwd", argv[0]) == 0) {
        if (argc > 1) {
            fprintf(2, "pwd: expected 0 arguments; got %d\n", argc - 1);
        } else {
            printf("%s\n", cwd);
        }

        return;
    }
    if (strcmp("history", argv[0]) == 0) {
        if (argc > 1) {
            fprintf(2, "history: expected 0 arguments; got %d\n", argc - 1);
        } else {
            show_history(&history);
        }

        return;
    }

    int child = spawn(argv[0], argv);
    if (child >= 0) {
        DEBUGF("spawn %s: %d\n", argv[0], child);
        wait(child);
    } else {
        fprintf(2, "spawn %s: %d\n", argv[0], child);
    }
    if (rightpipe) {
        close(1);  // close write end of pipe
        wait(rightpipe);
    }
    if (isChild) {
        // child process, exit
        exit();
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
            exit();
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
    exit();
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

        char* cmd = process_backticks(buf);
        runcmd(cmd);
        // restore original 0/1 fd
        restore_01(storedFd);
    }
    return 0;
}
