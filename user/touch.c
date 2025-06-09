#include <lib.h>

int main(int argc, char **argv) {
	int i, fd, r;

	if (argc < 2) {
		printf("Usage: touch <file1> [file2 ...]\n");
		exit();
	}

	for (i = 1; i < argc; i++) {
		// Attempt to create the file exclusively, opening in write-only mode.
		fd = open(argv[i], O_WRONLY | O_CREAT | O_EXCL);
        // debugf("touch %s get %d\n", argv[i], fd);

		if (fd >= 0) {
			// File was created successfully.
			r = close(fd);
			if (r < 0) {
				printf("touch: failed to close %s (error %d)\n", argv[i], r);
			}
		} else if (fd == -E_NOT_FOUND) {
            printf("touch: cannot touch '%s': No such file or directory\n", argv[i]);
		}
	}

	return 0;
}
