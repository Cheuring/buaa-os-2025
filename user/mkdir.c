#include <lib.h>


static void usage(void) {
	printf("usage: mkdir [-p] <directory...>\n");
	exit(1);
}

int main(int argc, char **argv) {
	int i;
	int p_flag = 0;
	int r;

	ARGBEGIN {
	case 'p':
		p_flag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc == 0) {
		usage();
	}

	for(i = 0; i < argc; ++i) {
		if((r = mkdir(argv[i], p_flag)) < 0) {
			// debugf("mkdir %s: %d\n", argv[i], r);
			if(r == -E_FILE_EXISTS && !p_flag) {
				fprintf(2, "mkdir: cannot create directory '%s': File exists\n", argv[i]);
				exit(1);
			} else if(r == -E_NOT_FOUND) {
				fprintf(2, "mkdir: cannot create directory '%s': No such file or directory\n", argv[i]);
				exit(1);
			}
		}
	}

	return 0;
}
