#include <args.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"
#define PROMPT "mos> "
#define MAX_COMMAND_LENGTH 1024
#define DEL 0x7f
#define ESC 0x1b
#define BACKSPACE 0x08
#define C(x) ((x)-'@') // Control-x

#define PUT_CHAR(c) \
	do { \
		if ((c) < 32 || (c) >= 127) { \
			printf("?"); \
		} else { \
			printf("%c", (c)); \
		} \
	} while (0)

#define CHECK_FD(fd) \
	do { \
		if ((fd) < 0) { \
			debugf("bad fd %d\n", (fd)); \
			return; \
		} \
	} while (0)

// history structure
static struct {
	#define MAX_HISTORY_COMMANDS 20
	#define HISTORY_FILE "/.mosh_history"

	int fd; // file descriptor for history file
	int write_index; // next write index in history.buffer
	int cursor; // moving cursor in history.buffer
	char buffer[MAX_HISTORY_COMMANDS][MAX_COMMAND_LENGTH]; // command history buffer
	char stage_command[MAX_COMMAND_LENGTH]; // current command being edited
} history;

static char cwd[MAXPATHLEN];

// using buffer, read a line from fd into buf
int fgetline(int fd, char *buf, u_int n) {
	char _buf[32];
	int r;
	u_int i = 0;

	if (n == 0) {
		return 0; // no space to read
	}

	while (i < n - 1) {
		if ((r = read(fd, _buf, sizeof(_buf))) <= 0) {
			if (r < 0) {
				debugf("read error: %d\n", r);
			}
			break;
		}
		for (int j = 0; j < r; j++) {
			if (_buf[j] == '\n') {
				buf[i++] = '\0';
				return i;
			} else if (_buf[j] == '\r') {
				continue; // ignore carriage return
			} else {
				buf[i++] = _buf[j];
				if(i == n - 1) {
					break;
				}
			}
		}
	}
	buf[i] = '\0';

	if (i == n - 1) {
		debugf("line too long\n");
		return -1;
	}

	return i;
}

void load_command_history() {
	history.write_index = 0;

	if ((history.fd = open(HISTORY_FILE, O_RDWR | O_CREAT)) < 0) {
		debugf("failed to open history file: %s\n", HISTORY_FILE);
		return;
	}

	while (history.write_index < MAX_HISTORY_COMMANDS && fgetline(history.fd, history.buffer[history.write_index], MAX_COMMAND_LENGTH) > 0) {
		++history.write_index;
	}

	// set cursor to the end of history
	history.cursor = history.write_index;
}

void save_command_history() {
	CHECK_FD(history.fd);

	// always write from start
	seek(history.fd, 0);

	if(history.write_index < MAX_HISTORY_COMMANDS) {
		// if we have less than MAX_HISTORY_COMMANDS, write only the used part
		for (int i = 0; i < history.write_index; ++i) {
			write(history.fd, history.buffer[i], strlen(history.buffer[i]));
			write(history.fd, "\n", 1);
		}
	} else {
		// write from earliest to latest
		for (int i = 0; i < MAX_HISTORY_COMMANDS; ++i) {
			int idx = (history.write_index + i) % MAX_HISTORY_COMMANDS;
			write(history.fd, history.buffer[idx], strlen(history.buffer[idx]));
			write(history.fd, "\n", 1);
		}
	}
}

void move_history_cursor(char *buf, int *edit_idx, int offset) {
	CHECK_FD(history.fd);

	int cur = history.cursor + offset;
	if(cur < 0 || cur <= history.write_index - MAX_HISTORY_COMMANDS) {
		// out of bounds
		debugf("history at the top: %d\n", cur);
		return;
	}

	if(cur == history.write_index) {
		// if we are at the end, just pop the stage command
		memcpy(buf, history.stage_command, MAX_COMMAND_LENGTH);
		*edit_idx = strlen(buf);
		history.cursor = cur;
		return;
	}

	if(cur > history.write_index) {
		// out of bounds
		debugf("history at the bottom: %d\n", cur);
		return;
	}

	memcpy(buf, history.buffer[cur % MAX_HISTORY_COMMANDS], MAX_COMMAND_LENGTH);
	*edit_idx = strlen(buf);
	history.cursor = cur;
}

void stage_command(char *buf, int *i, char *backbuf, int *backbuf_i) {
	CHECK_FD(history.fd);

	// not stage edited history 
	if(history.cursor != history.write_index) {
		return;
	}

	// copy the current command to the stage command
	memcpy(history.stage_command, buf, *i);
	memcpy(history.stage_command + (*i), backbuf, *backbuf_i);
	history.stage_command[(*i) + (*backbuf_i)] = '\0';

	// reset index
	*i = 0;
	*backbuf_i = 0;
}

void add_history(char *buf) {
	CHECK_FD(history.fd);
	int idx = history.write_index % MAX_HISTORY_COMMANDS;

	// if the command is empty, do nothing
	if (buf[0] == '\0') {
		return;
	}

	// add the command to the history buffer
	memcpy(history.buffer[idx], buf, MAX_COMMAND_LENGTH);
	// history.buffer[idx][MAX_COMMAND_LENGTH - 1] = '\0'; // ensure null termination
	history.write_index++;
	history.cursor = history.write_index;

	// save the history to file
	save_command_history();
}

void show_history() {
	CHECK_FD(history.fd);

	// print the history buffer
	int i = history.write_index - MAX_HISTORY_COMMANDS;
	if (i < 0) {
		i = 0;
	}

	while(i < history.write_index) {
		int idx = i % MAX_HISTORY_COMMANDS;
		printf("%s\n", history.buffer[idx]);
		i++;
	}
}

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
			debugf("reached here, fd: %d\n", fd);
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
	
	// todo: fix bug
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
			if((r = chdir(argv[1])) < 0) {
				debugf("chdir %s: %d\n", argv[1], r);
			} else {
				strcpy(cwd, env->cwd_name);
			}

			break;
		
		default:
			printf("Too many args for cd command\n");
		}

		return;
	}
	if (strcmp("pwd", argv[0]) == 0) {
		if(argc > 1) {
			printf("pwd: expected 0 arguments; got %d\n", argc - 1);
		}else{
			printf("%s\n", cwd);
		}

		return;
	}
	if (strcmp("history", argv[0]) == 0) {
		if(argc > 1) {
			printf("history: expected 0 arguments; got %d\n", argc - 1);
		}else{
			show_history();
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
			case BACKSPACE:
			case DEL:
				// delete
				if(i > 0){
					i--;
					printf("\b");
					for(int k = backbuf_i - 1; k >= 0; k--){
						PUT_CHAR(backbuf[k]);
					}
					printf(" \033[%dD", backbuf_i + 1);
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
	
			case ESC:
				state = GOT_ESC;
				break;

			case C('E'):
				// move cursor to the end
				if(backbuf_i > 0) {
					printf("\033[%dC", backbuf_i);
					for(int k = backbuf_i - 1; k >= 0; k--){
						buf[i++] = backbuf[k];
					}
					backbuf_i = 0;
				}
				break;

			case C('A'):
				// move cursor to the start
				if(i > 0) {
					printf("\033[%dD", i);
					for(int k = i - 1; k >= 0; --k){
						backbuf[backbuf_i++] = buf[k];
					}
					i = 0;
				}
				break;

			case C('K'):
				// clear line from cursor to end
				printf("\033[K");
				backbuf_i = 0;
				break;

			case C('U'):
				// clear line from start to cursor
				if(i > 0) {
					printf("\r\033[K%s", PROMPT);
					for(int k = backbuf_i - 1; k >= 0; k--){
						PUT_CHAR(backbuf[k]);
					}
					if(backbuf_i > 0) {
						printf("\033[%dD", backbuf_i);
					}
					i = 0;
				}
				break;

			case C('W'):
				// delete word
				{
					int temp_i = i;
					while (temp_i > 0 && strchr(WHITESPACE, buf[temp_i - 1])) {
						temp_i--;
					}
					while (temp_i > 0 && !strchr(WHITESPACE, buf[temp_i - 1])) {
						temp_i--;
					}

					if(i > temp_i) {
						// move cursor
						printf("\033[%dD", i - temp_i);
						// clear till end of line
						printf("\033[K");
						// redraw the back buffer
						for(int k = backbuf_i - 1; k >= 0; k--){
							PUT_CHAR(backbuf[k]);
						}
						// remove the word from the buffer
						if(backbuf_i > 0) {
							printf("\033[%dD", backbuf_i);
						}

						i = temp_i;
					}
				}
				break;
	
			default:
				buf[i++] = c;
				PUT_CHAR(c);

				for(int k = backbuf_i - 1; k >= 0; k--){
					PUT_CHAR(backbuf[k]);
				}

				if(backbuf_i > 0) {
					printf("\033[%dD", backbuf_i);
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
				stage_command(buf, &i, backbuf, &backbuf_i);
				// get previous command
				move_history_cursor(buf, &i, -1);
				// print the command
				printf("\r\033[K%s%s", PROMPT, buf);
				break;
			case 'B':
				// down arrow
				// stage command
				stage_command(buf, &i, backbuf, &backbuf_i);
				// get next command
				move_history_cursor(buf, &i, 1);
				// print the command
				printf("\r\033[K%s%s", PROMPT, buf);
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
				printf("[");
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
	load_command_history();
	strcpy(cwd, env->cwd_name);
	for (;;) {
		if (interactive) {
			printf("\n%s", PROMPT);
		}
		readline(buf, sizeof buf);
		add_history(buf);

		if (buf[0] == '#') {
			continue;
		}
		if (echocmds) {
			printf("# %s\n", buf);
		}
		// if ((r = fork()) < 0) {
		// 	user_panic("fork: %d", r);
		// }
		// if (r == 0) {
		// 	runcmd(buf);
		// 	exit();
		// } else {
		// 	wait(r);
		// }
		runcmd(buf);
	}
	return 0;
}
