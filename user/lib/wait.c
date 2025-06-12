#include <env.h>
#include <lib.h>
int wait(u_int envid) {
	const volatile struct Env *e;
	int r;

	e = &envs[ENVX(envid)];
	while (e->env_id == envid && e->env_status != ENV_DYING) {
		syscall_yield();
	}
	
	// must first store exit_status, then set env_status to ENV_FREE
	r = e->exit_status;
	syscall_set_env_status(envid, ENV_FREE);
	return r;
}
