#include <lib.h>

char buf[8192];

void cat(int f, char *s) {
	long n;
	int r;

	while ((n = read(f, buf, (long)sizeof buf)) > 0) {
		if ((r = write(1, buf, n)) != n) {
			fprintf(2, "write error copying %s: %d", s, r);
			exit(1);
		}
	}
	if (n < 0) {
		fprintf(2, "error reading %s: %d", s, n);
		exit(1);
	}
}

int main(int argc, char **argv) {
	int f, i;

	if (argc == 1) {
		cat(0, "<stdin>");
	} else {
		for (i = 1; i < argc; i++) {
			f = open(argv[i], O_RDONLY);
			if (f < 0) {
				fprintf(2, "can't open %s: %d", argv[i], f);
				exit(1);
			} else {
				cat(f, argv[i]);
				close(f);
			}
		}
	}
	return 0;
}
