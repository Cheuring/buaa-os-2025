#include <args.h>
#include <history.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"
#define PROMPT "mos> "
#define DEL 0x7f
#define ESC 0x1b
#define BACKSPACE 0x08
#define C(x) ((x) - '@')  // Control-x

static struct History history;
static char cwd[MAXPATHLEN];
static int interactive;

#define PRINTF(...)                 \
    do {                            \
        if (interactive) {          \
            printf(__VA_ARGS__);    \
        }                           \
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

int isDir(const char* path) {
    struct Stat st;
    int r;
    if ((r = stat(path, &st)) < 0) {
        // debugf("stat '%s': %d\n", path, r);
        return r;
    }

    return st.st_isdir;
}

int parsecmd(char **argv, int *rightpipe) {
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
                    debugf("too many arguments\n");
                    exit();
                }
                argv[argc++] = t;
                break;
            case '<':
                if (gettoken(0, &t) != 'w') {
                    debugf("syntax error: < not followed by word\n");
                    exit();
                }

                r = isDir(t);
                if (r == 1) {
                    fprintf(2, "bash: %s: Is a directory\n", t);
                    return 0;
                }

                if ((fd = open(t, O_RDONLY)) < 0) {
                    debugf("failed to open '%s'\n", t);
                    exit();
                }
                dup(fd, 0);
                close(fd);

                break;
            case '>':
                if (gettoken(0, &t) != 'w') {
                    debugf("syntax error: > not followed by word\n");
                    exit();
                }

                r = isDir(t);
                if(r == 1) {
                    fprintf(2, "bash: %s: Is a directory\n", t);
                    return 0;
                }

                if ((fd = open(t, O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
                    debugf("failed to open '%s'\n", t);
                    exit();
                }
                dup(fd, 1);
                close(fd);

                break;
            case '|':;
                /*
                 * First, allocate a pipe.
                 * Then fork, set '*rightpipe' to the returned child envid or
                 * zero. The child runs the right side of the pipe:
                 * - dup the read end of the pipe onto 0
                 * - close the read end of the pipe
                 * - close the write end of the pipe
                 * - and 'return parsecmd(argv, rightpipe)' again, to parse the
                 * rest of the command line. The parent runs the left side of
                 * the pipe:
                 * - dup the write end of the pipe onto 1
                 * - close the write end of the pipe
                 * - close the read end of the pipe
                 * - and 'return argc', to execute the left of the pipeline.
                 */
                int p[2];
                /* Exercise 6.5: Your code here. (3/3) */
                if ((r = pipe(p)) != 0) {
                    debugf("pipe: %d\n", r);
                    exit();
                }
                r = fork();
                if (r < 0) {
                    debugf("fork: %d\n", r);
                    exit();
                }
                *rightpipe = r;
                if (r == 0) {
                    dup(p[0], 0);
                    close(p[0]);
                    close(p[1]);
                    return parsecmd(argv, rightpipe);
                } else {
                    dup(p[1], 1);
                    close(p[1]);
                    close(p[0]);
                    return argc;
                }

                break;
        }
    }

    return argc;
}

void runcmd(char *s) {
    gettoken(s, 0);

    char *argv[MAXARGS];
    int rightpipe = 0;

    int argc = parsecmd(argv, &rightpipe);
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
                    debugf("chdir %s: %d\n", argv[1], r);
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
    // close_all();
    if (child >= 0) {
        wait(child);
    } else {
        debugf("spawn %s: %d\n", argv[0], child);
    }
    if (rightpipe) {
        wait(rightpipe);
    }
    // exit();
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
                debugf("read error: %d\n", r);
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
                        if(interactive){
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
                        if(interactive){
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
                            if(interactive){
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

                default:
                    buf[i++] = c;
                    if(interactive){
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
    debugf("line too long\n");
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
    int r, p[2];
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
        printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
        printf("::                                                         ::\n");
        printf("::                     MOS Shell 2024                      ::\n");
        printf("::                                                         ::\n");
        printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
    }

    // store original 0/1 fd
    if ((r = pipe(p)) < 0) {
        user_panic("pipe: %d", r);
    }
    if ((r = dup(0, p[0])) < 0) {
        user_panic("dup: %d", r);
    }
    if ((r = dup(1, p[1])) < 0) {
        user_panic("dup: %d", r);
    }

    load_command_history(&history);
    strcpy(cwd, (const char *)env->cwd_name);
    for (;;) {
        PRINTF("\n%s", PROMPT);
        readline(buf, sizeof buf);
        add_history(&history, buf);

        comment = (char *)strchr(buf, '#');
        if (comment) {
            *comment = 0; // truncate at comment
        }
        if (echocmds) {
            printf("# %s\n", buf);
        }

        runcmd(buf);
        // restore original 0/1 fd
        if ((r = dup(p[0], 0)) < 0) {
            user_panic("dup restore: %d", r);
        }
        if ((r = dup(p[1], 1)) < 0) {
            user_panic("dup restore: %d", r);
        }
    }
    return 0;
}
