#include <env.h>
#include <lib.h>
#include <mmu.h>

void exit(int status) {
	// After fs is ready (lab5), all our open files should be closed before dying.
#if !defined(LAB) || LAB >= 5
	close_all();
#endif

	DEBUGF("exiting env %d status: %d\n", env->env_id, status);
	syscall_set_exit_status(status);
	syscall_env_destroy(0);
	user_panic("unreachable code");
}

const volatile struct Env *env;
extern int main(int, char **);

void libmain(int argc, char **argv) {
	int r;
	// set env to point at our env structure in envs[].
	env = &envs[ENVX(syscall_getenvid())];

	// call user main routine
	r = main(argc, argv);

	// exit gracefully
	exit(r);
}
