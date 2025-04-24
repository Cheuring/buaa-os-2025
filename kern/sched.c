#include "queue.h"
#include <env.h>
#include <pmap.h>
#include <printk.h>

/* Overview:
 *   Implement a round-robin scheduling to select a runnable env and schedule it using 'env_run'.
 *
 * Post-Condition:
 *   If 'yield' is set (non-zero), 'curenv' should not be scheduled again unless it is the only
 *   runnable env.
 *
 * Hints:
 *   1. The variable 'count' used for counting slices should be defined as 'static'.
 *   2. Use variable 'env_sched_list', which contains and only contains all runnable envs.
 *   3. You shouldn't use any 'return' statement because this function is 'noreturn'.
 */
void schedule_rr(int yield) {
	static int count = 0; // remaining time slices of current env
	static struct Env* e = NULL;

	/* We always decrease the 'count' by 1.
	 *
	 * If 'yield' is set, or 'count' has been decreased to 0, or 'e' (previous 'curenv') is
	 * 'NULL', or 'e' is not runnable, then we pick up a new env from 'env_sched_list' (list of
	 * all runnable envs), set 'count' to its priority, and schedule it with 'env_run'. **Panic
	 * if that list is empty**.
	 *
	 * (Note that if 'e' is still a runnable env, we should move it to the tail of
	 * 'env_sched_list' before picking up another env from its head, or we will schedule the
	 * head env repeatedly.)
	 *
	 * Otherwise, we simply schedule 'e' again.
	 *
	 * You may want to use macros below:
	 *   'TAILQ_FIRST', 'TAILQ_REMOVE', 'TAILQ_INSERT_TAIL'
	 */
	/* Exercise 3.12: Your code here. */
	--count;
	if(yield || count == 0 || e == NULL || e->env_status != ENV_RUNNABLE){
		if(e != NULL && e->env_status == ENV_RUNNABLE){
			TAILQ_REMOVE(&env_sched_list, e, env_sched_link);
			TAILQ_INSERT_TAIL(&env_sched_list, e, env_sched_link);
		}
		
		e = TAILQ_FIRST(&env_sched_list);
		panic_on(e == NULL);
		count = e->env_pri;
	}
	env_run(e);
}


void schedule(int yield){
	static int clock = -1;
	clock++;

	struct Env *temp, *earliest = NULL;
	LIST_FOREACH(temp, &env_edf_sched_list, env_edf_sched_link){
		if(clock == temp->env_period_deadline){
			temp->env_period_deadline += temp->env_edf_period;
			temp->env_runtime_left = temp->env_edf_runtime;
		}
	}

	LIST_FOREACH(temp, &env_edf_sched_list, env_edf_sched_link){
		if(temp->env_runtime_left > 0){
			if(earliest == NULL ||
					temp->env_period_deadline < earliest->env_period_deadline ||
					(temp->env_period_deadline == earliest->env_period_deadline && temp->env_id < earliest->env_id)){
				earliest = temp;
			}
		}
	}

	if(earliest == NULL){
		schedule_rr(yield);
		return;
	}

	earliest->env_runtime_left--;
	env_run(earliest);
}
