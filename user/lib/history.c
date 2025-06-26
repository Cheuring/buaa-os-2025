#include <lib.h>
#include <history.h>

// todo: improve this
int fgetline(int fd, char *buf, u_int n) {
	int i = 0;
	char c;

	while (i < n - 1 && read(fd, &c, 1) > 0) {
		if (c == '\n') {
			break;
		}
		buf[i++] = c;
	}

	buf[i] = '\0';

	if (i == 0 && c != '\n') {
		return -1; // no data read
	}

	return i; // return number of characters read
}

void load_command_history(struct History *history) {
	history->write_index = 0;

	if ((history->fd = open(HISTORY_FILE, O_RDWR | O_CREAT)) < 0) {
		debugf("failed to open history file: %s\n", HISTORY_FILE);
		return;
	}

	while (history->write_index < MAX_HISTORY_COMMANDS && fgetline(history->fd, history->buffer[history->write_index], MAX_COMMAND_LENGTH) > 0) {
		++history->write_index;
	}

	// set cursor to the end of history
	history->cursor = history->write_index;
}

void save_command_history(struct History *history) {
	CHECK_FD(history->fd);

	// always write from start
	seek(history->fd, 0);

	if(history->write_index < MAX_HISTORY_COMMANDS) {
		// if we have less than MAX_HISTORY_COMMANDS, write only the used part
		for (int i = 0; i < history->write_index; ++i) {
			write(history->fd, history->buffer[i], strlen(history->buffer[i]));
			write(history->fd, "\n", 1);
		}
	} else {
		// write from earliest to latest
		for (int i = 0; i < MAX_HISTORY_COMMANDS; ++i) {
			int idx = (history->write_index + i) % MAX_HISTORY_COMMANDS;
			write(history->fd, history->buffer[idx], strlen(history->buffer[idx]));
			write(history->fd, "\n", 1);
		}
	}
}

static void stage_command(struct History *history, char *buf, int i, char *backbuf, int backbuf_i) {
	// if is not new command, do nothing
	if (history->cursor != history->write_index) {
		return;
	}

	// copy the current command to the stage command
	memcpy(history->stage_command, buf, i);
	for (int j = backbuf_i - 1; j >= 0; j--) {
		history->stage_command[i++] = backbuf[j];
	}
	history->stage_command[i] = '\0';
}

void move_history_cursor(struct History *history, char *buf, int *i, char *backbuf, int *backbuf_i, int offset) {
	CHECK_FD(history->fd);

	int cur = history->cursor + offset;
	if(cur < 0 || cur <= history->write_index - MAX_HISTORY_COMMANDS) {
		// out of bounds
		buf[*i] = 0;
		DEBUGF("history at the top: %d\n", cur);
		return;
	}

	if(cur == history->write_index) {
		// if we are at the end, just pop the stage command
		memcpy(buf, history->stage_command, MAX_COMMAND_LENGTH);
		goto out;
	}

	if(cur > history->write_index) {
		// out of bounds
		buf[*i] = 0;
		DEBUGF("history at the bottom: %d\n", cur);
		return;
	}

	// stage the current command
	stage_command(history, buf, *i, backbuf, *backbuf_i);
	// pop the command from history
	memcpy(buf, history->buffer[cur % MAX_HISTORY_COMMANDS], MAX_COMMAND_LENGTH);
out:
	*i = strlen(buf);
	history->cursor = cur;
	*backbuf_i = 0;
}

void add_history(struct History *history, char *buf) {
	CHECK_FD(history->fd);
	int idx = history->write_index % MAX_HISTORY_COMMANDS;

	// if the command is empty, do nothing
	if (buf[0] == '\0') {
		return;
	}

	// add the command to the history buffer
	memcpy(history->buffer[idx], buf, MAX_COMMAND_LENGTH);
	// history->buffer[idx][MAX_COMMAND_LENGTH - 1] = '\0'; // ensure null termination
	history->write_index++;
	history->cursor = history->write_index;

	// save the history to file
	save_command_history(history);
}

void show_history(struct History *history) {
	CHECK_FD(history->fd);

	// print the history buffer
	int i = history->write_index - MAX_HISTORY_COMMANDS;
	if (i < 0) {
		i = 0;
	}

	while(i < history->write_index) {
		int idx = i % MAX_HISTORY_COMMANDS;
		printf("%s\n", history->buffer[idx]);
		i++;
	}
}