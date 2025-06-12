#include <history.h>
#include <lib.h>
#include <variable.h>

int is_valid_var_name(const char *);
int _is_full(struct VariableSet *);
void _set_value(struct Variable *, const char *);
struct Variable *_find_var(struct VariableSet *vset, const char *name);

static int va_is_mapped(void *va) {
    return (vpd[PDX(va)] & PTE_V) && (vpt[VPN(va)] & PTE_V);
}

void init_vars(struct VariableSet *vset) {
    struct VariableSet *parent_var;
    int r;

    if (vset == NULL) return;

    vset->exportIdx = 0;        // Initialize export index
    vset->localIdx = MAX_VARS;  // Initialize local index to the end
    memset(vset->vars, 0, sizeof(vset->vars));  // Clear the variable set
    panic_on(syscall_set_variable_set((void *)vset));
    if (va_is_mapped((void *)UTEMP)) {
        parent_var = (struct VariableSet *)UTEMP;
        copy_vars(vset, parent_var);
        if ((r = syscall_mem_unmap(0, (void *)UTEMP)) < 0) {
            user_panic("init_vars: syscall_mem_unmap failed: %d", r);
        }

        DEBUGF("init_vars: copied parent variable set. %d %d\n",
               vset->exportIdx, vset->localIdx);
    }
}

struct Variable *_find_var(struct VariableSet *vset, const char *name) {
    if (name == NULL || vset == NULL) {
        return NULL;
    }
    for (int i = 0; i < vset->exportIdx; i++) {
        if (strcmp(vset->vars[i].name, name) == 0) {
            return &vset->vars[i];
        }
    }
    for (int i = MAX_VARS - 1; i >= vset->localIdx; i--) {
        if (strcmp(vset->vars[i].name, name) == 0) {
            return &vset->vars[i];
        }
    }
    return NULL;
}

int declare_var(struct VariableSet *vset, char *name, char *value,
                int export_flag, int readonly_flag) {
    if (!is_valid_var_name(name)) {
        fprintf(2, "Error: Invalid variable name '%s'.\n", name);
        return -E_INVAL;
    }

    struct Variable *var = _find_var(vset, name);

    if (var) {  // Variable exists
        if (var->mode & V_RDONLY) {
            fprintf(2, "Error: Variable '%s' is read-only.\n", name);
            return -E_NOT_WRITABLE;
        }
        // Update value
        _set_value(var, value);
        var->mode |= V_SET;
        if (export_flag) var->mode |= V_EXPORT;
        // Cannot remove export flag if already exported and trying to make it
        // local Cannot remove readonly flag
        if (readonly_flag) var->mode |= V_RDONLY;

    } else {  // New variable
        if (_is_full(vset)) {
            fprintf(2, "Error: Variable set is full.\n");
            return -E_NO_MEM;
        }

        var = export_flag ? &vset->vars[vset->exportIdx++]
                          : &vset->vars[--vset->localIdx];
        strncpy(var->name, name, MAX_VAR_NAME_LEN);
        var->name[MAX_VAR_NAME_LEN] = '\0';

        _set_value(var, value);
        var->mode = V_SET;
        if (export_flag) var->mode |= V_EXPORT;
        if (readonly_flag) var->mode |= V_RDONLY;
    }
    return 0;
}

int unset_var(struct VariableSet *vset, char *name) {
    if (vset == NULL || name == NULL) {
        return -E_INVAL;
    }
    struct Variable *var_to_remove = NULL;
    int idx_to_remove = -1;
    int is_export = 0;

    var_to_remove = _find_var(vset, name);
    if (!var_to_remove) {
        fprintf(2, "Error: Variable '%s' not found.\n",
                name);  // Optional: POSIX
        // unset doesn't error if var not found
        return 0;
    }

    idx_to_remove = var_to_remove - vset->vars;
    is_export = (idx_to_remove < vset->exportIdx);

    if (var_to_remove->mode & V_RDONLY) {
        printf("Error: Variable '%s' is read-only.\n", name);
        return -E_NOT_WRITABLE;
    }

    // Clear the variable
    memset(var_to_remove, 0, sizeof(struct Variable));

    // Shift elements if necessary
    if (is_export) {
        for (int i = idx_to_remove; i < vset->exportIdx - 1; i++) {
            vset->vars[i] = vset->vars[i + 1];
        }
        if (vset->exportIdx > 0) {
            memset(&vset->vars[vset->exportIdx - 1], 0,
                   sizeof(struct Variable));  // Clear the last one
            vset->exportIdx--;
        }
    } else {  // is_local
        for (int i = idx_to_remove; i > vset->localIdx; i--) {
            vset->vars[i] = vset->vars[i - 1];
        }
        if (vset->localIdx < MAX_VARS) {
            memset(&vset->vars[vset->localIdx], 0,
                   sizeof(struct Variable));  // Clear the first one
            vset->localIdx++;
        }
    }
    return 0;
}

void _print_var(struct Variable *var) {
    if (var == NULL) return;
    // printf(
    //     "%c%c  %s=%s\n",
    //     (var->mode & V_EXPORT) ? 'E' : 'L',  // E for exported, L for local
    //     (var->mode & V_RDONLY) ? 'R' : ' ',  // R for read-only, space otherwise
    //     var->name, var->value);
    printf("%s=%s\n", var->name, var->value);
}

void print_vars(struct VariableSet *vset) {
    if (vset == NULL) return;
    // Print exported variables
    for (int i = 0; i < vset->exportIdx; i++) {
        _print_var(&vset->vars[i]);
    }
    // Print local variables
    for (int i = MAX_VARS - 1; i >= vset->localIdx; i--) {
        _print_var(&vset->vars[i]);
    }
}

// Basic variable expansion: $VAR
// Does not handle complex cases like $VAR_SUFFIX or escaping.
int expand_vars(struct VariableSet *vset, char *line) {
    if (vset == NULL || line == NULL) {
        return -E_INVAL;
    }

    char expanded_line[MAX_COMMAND_LENGTH];
    char var_name[MAX_VAR_NAME_LEN + 1];
    char *p = line;
    char *q = expanded_line;
    int remaining_len = MAX_COMMAND_LENGTH - 1;  // For null terminator

    while (*p && remaining_len > 0) {
        if (*p == '$') {
            p++;  // Skip '$'
            int i = 0;

            while (*p && (isalnum(*p) || *p == '_') && i < MAX_VAR_NAME_LEN) {
                var_name[i++] = *p++;
            }
            var_name[i] = '\0';

            if (i > 0) {  // Found a potential variable name
                struct Variable *var = _find_var(vset, var_name);
                if (var) {
                    panic_on((var->mode & V_SET) == 0);
                    char *val = var->value;
                    while (*val && remaining_len > 0) {
                        *(q++) = *(val++);
                        remaining_len--;
                    }
                } else {
                    // Variable not found or not set, expand to empty string
                    // (common behavior) Or, if you want to keep $VARNAME as is:
                    // *(q++) = '$'; remaining_len--;
                    // char *temp_vn_ptr = var_name;
                    // while(*temp_vn_ptr && remaining_len > 0) { *(q++) =
                    // *(temp_vn_ptr++); remaining_len--;}
                }
            } else if (*(p - 1) == '$') {  // Just a '$' sign
                if (remaining_len > 0) {
                    *(q++) = '$';
                    remaining_len--;
                }
            }
            // If var_name was empty (e.g., $ followed by space or end of line),
            // the '$' was already skipped. If it was just '$', it's handled
            // above.
        } else {
            if (remaining_len > 0) {
                *(q++) = *(p++);
                remaining_len--;
            } else {
                break;  // Output buffer full
            }
        }
    }
    if (remaining_len <= 0) {
        debugf("error might occur\n");
    }
    *q = '\0';
    // Copy the expanded line back to the original line
    strcpy(line, expanded_line);
    return 0;
}

void copy_vars(struct VariableSet *dst, struct VariableSet *src) {
    if (dst == NULL || src == NULL) return;

    // Copy exported variables
    for (int i = 0; i < src->exportIdx; i++) {
        if (src->vars[i].mode &
            V_EXPORT) {  // Only copy if it's truly an exported var
            dst->vars[dst->exportIdx] = src->vars[i];  // Direct struct copy
            dst->exportIdx++;
        } else {
            user_panic("copy_vars: src->vars[%d] is not exported", i);
        }
    }
    // Local variables are not inherited, so src->localIdx part is not copied.
}

int _is_full(struct VariableSet *vset) {
    if (vset == NULL) return 1;  // Treat null as full
    return vset->exportIdx > vset->localIdx;
}

void _set_value(struct Variable *var, const char *value) {
    if (value) {
        strncpy(var->value, value, MAX_VAR_VALUE_LEN);
        var->value[MAX_VAR_VALUE_LEN] = '\0';
    } else {
        var->value[0] = '\0';  // Set to empty string
    }
}

int is_valid_var_name(const char *name) {
    if (!name || strlen(name) == 0 || strlen(name) > MAX_VAR_NAME_LEN) {
        return 0;
    }

    // 第一个字符必须是字母或下划线
    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_')) {
        return 0;
    }

    // 其余字符必须是字母、数字或下划线
    for (int i = 1; name[i]; i++) {
        if (!(isalnum(name[i]) || name[i] == '_')) {
            return 0;
        }
    }
    return 1;
}