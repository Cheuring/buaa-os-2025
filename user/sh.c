#include <args.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"
#define DEL 0x7f

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
 *   The buffer is modified to turn the spaces after words into zero bytes ('\0'), so that the
 *   returned token is a null-terminated string.
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

int parsecmd(char **argv, int *rightpipe) {
	int argc = 0;
	while (1) {
		char *t;
		int fd, r;
		int c = gettoken(0, &t);
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
			// Open 't' for reading, dup it onto fd 0, and then close the original fd.
			// If the 'open' function encounters an error,
			// utilize 'debugf' to print relevant messages,
			// and subsequently terminate the process using 'exit'.
			/* Exercise 6.5: Your code here. (1/3) */
			if((fd = open(t, O_RDONLY)) < 0){
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
			// Open 't' for writing, create it if not exist and trunc it if exist, dup
			// it onto fd 1, and then close the original fd.
			// If the 'open' function encounters an error,
			// utilize 'debugf' to print relevant messages,
			// and subsequently terminate the process using 'exit'.
			/* Exercise 6.5: Your code here. (2/3) */
			if((fd = open(t, O_WRONLY | O_CREAT | O_TRUNC)) < 0){
				debugf("failed to open '%s'\n", t);
				exit();
			}
			dup(fd, 1);
			close(fd);

			break;
		case '|':;
			/*
			 * First, allocate a pipe.
			 * Then fork, set '*rightpipe' to the returned child envid or zero.
			 * The child runs the right side of the pipe:
			 * - dup the read end of the pipe onto 0
			 * - close the read end of the pipe
			 * - close the write end of the pipe
			 * - and 'return parsecmd(argv, rightpipe)' again, to parse the rest of the
			 *   command line.
			 * The parent runs the left side of the pipe:
			 * - dup the write end of the pipe onto 1
			 * - close the write end of the pipe
			 * - close the read end of the pipe
			 * - and 'return argc', to execute the left of the pipeline.
			 */
			int p[2];
			/* Exercise 6.5: Your code here. (3/3) */
			if((r = pipe(p)) != 0){
				debugf("pipe: %d\n", r);
				exit();
			}
			r = fork();
			if(r < 0){
				debugf("fork: %d\n", r);
				exit();
			}
			*rightpipe = r;
			if(r == 0){
				dup(p[0], 0);
				close(p[0]);
				close(p[1]);
				return parsecmd(argv, rightpipe);
			}else{
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

	int child = spawn(argv[0], argv);
	close_all();
	if (child >= 0) {
		wait(child);
	} else {
		debugf("spawn %s: %d\n", argv[0], child);
	}
	if (rightpipe) {
		wait(rightpipe);
	}
	exit();
}

void readline(char *buf, u_int n) {
	int r, i = 0;
	char c;
	static char backbuf[1024];
	int backbuf_i = 0;
	static enum {NORMAL, GOT_ESC, GOT_BRACKET} state = NORMAL;

	while (i + backbuf_i < n - 1) {
		if ((r = read(0, &c, 1)) != 1) {
			if (r < 0) {
				debugf("read error: %d\n", r);
			}
			exit();
		}

back:
		if(state == NORMAL){
			switch (c) {
			case '\b':
			case DEL:
				// delete
				if(i > 0){
					i--;
					printf("\b");
					for(int k = backbuf_i - 1; k >= 0; k--){
						if(backbuf[k] < 32 || backbuf[k] >= 127) {
							printf("?");
						}else{
							printf("%c", backbuf[k]);
						}
					}
					printf(" ");

					for(int k = 0; k < backbuf_i + 1; k++){
						printf("\b");
					}
				}
				break;
	
			case '\r':
			case '\n':
				// new line
				printf("\n");
				for(int k = backbuf_i - 1; k >= 0; k--) {
					buf[i++] = backbuf[k];
				}
				buf[i] = 0;
				return;
	
			case 27: // escape
				state = GOT_ESC;
				break;
	
			default:
				buf[i++] = c;
				if (c < 32 || c >= 127) {
					// non-printable character
					printf("?");
				}else{
					printf("%c", c);
				}

				for(int k = backbuf_i - 1; k >= 0; k--){
					if(backbuf[k] < 32 || backbuf[k] >= 127) {
						printf("?");
					}else{
						printf("%c", backbuf[k]);
					}
				}

				for(int k = 0; k < backbuf_i; k++){
					printf("\b");
				}
			}
		}else if(state == GOT_ESC){
			if(c == '['){
				state = GOT_BRACKET;
			}else{
				state = NORMAL;
				// not a bracket, go back
				goto back;
			}
		}else{
			switch (c) {
			case 'A':
				// up arrow
				// stage command

				// get previous command

				break;
			case 'B':
				// down arrow
				// stage command

				// get next command

				break;
			case 'C':
				// right arrow
				// move cursor right
				if (backbuf_i > 0) {
					printf("\033[C");
					buf[i++] = backbuf[--backbuf_i];
				}
				break;
			case 'D':
				// left arrow
				// move cursor left
				if (i > 0) {
					printf("\033[D");
					backbuf[backbuf_i++] = buf[--i];
				}
				break;
			default:
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

char buf[1024];

void usage(void) {
	printf("usage: sh [-ix] [script-file]\n");
	exit();
}

int main(int argc, char **argv) {
	int r;
	int interactive = iscons(0);
	int echocmds = 0;
	printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	printf("::                                                         ::\n");
	printf("::                     MOS Shell 2024                      ::\n");
	printf("::                                                         ::\n");
	printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
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
		close(0);
		if ((r = open(argv[0], O_RDONLY)) < 0) {
			user_panic("open %s: %d", argv[0], r);
		}
		user_assert(r == 0);
	}
	for (;;) {
		if (interactive) {
			printf("\n$ ");
		}
		readline(buf, sizeof buf);

		if (buf[0] == '#') {
			continue;
		}
		if (echocmds) {
			printf("# %s\n", buf);
		}
		if ((r = fork()) < 0) {
			user_panic("fork: %d", r);
		}
		if (r == 0) {
			runcmd(buf);
			exit();
		} else {
			wait(r);
		}
	}
	return 0;
}
