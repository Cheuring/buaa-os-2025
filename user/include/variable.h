#ifndef _VARIABLE_H
#define _VARIABLE_H

#include <mmu.h>

#define MAX_VAR_NAME_LEN 16
#define MAX_VAR_VALUE_LEN 16
#define MAX_VARS (PAGE_SIZE / sizeof(struct Variable))  // 4096 / 40 = 102

#define V_SET 0x01
#define V_EXPORT 0x02
#define V_RDONLY 0x04

struct Variable {
    char name[MAX_VAR_NAME_LEN + 1];
    char value[MAX_VAR_VALUE_LEN + 1];
    int mode;
};

/*
 * |-------------------------------------------------------------------------------------------------|
 * | Global Env Var 0 | ... | Global Env Var N | ....... | Non-Global Var M | ... | Non-Global Var 0 |
 * |-------------------------------------------------------------------------------------------------|
 * 0                                           ^        ^                                      MAX_VARS-1
 *                                             |        |
 *                                       exportIdx    localIdx
*/
struct VariableSet {
    struct Variable vars[MAX_VARS];
    int exportIdx;  // next index for global env var
    int localIdx;   // next index for non-global var
};

void init_vars(struct VariableSet *vset);
int expand_vars(struct VariableSet *vset, char *line);
void copy_vars(struct VariableSet *dst, struct VariableSet *src);
void print_vars(struct VariableSet *vset);
int declare_var(struct VariableSet *vset, char *name, char *value, int export_flag, int readonly_flag);
int unset_var(struct VariableSet *vset, char *name);

#endif