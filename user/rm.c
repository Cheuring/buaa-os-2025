#include <lib.h>

void remove_file(char *path, int recursive, int force) {
    struct Stat st;
    int r, n;

    // 获取文件信息
    if ((r = stat(path, &st)) < 0) {
        if (!force) {
            fprintf(2, "rm: cannot remove '%s': No such file or directory\n",
                    path);
        }
        return;
    }

    // 检查是否为目录
    if (st.st_isdir) {
        if (!recursive) {
            fprintf(2, "rm: cannot remove '%s': Is a directory\n", path);
            return;
        }

        // 递归删除目录内容
        int fd;
        struct File f;

        if ((fd = open(path, O_RDONLY)) < 0) {
            if (!force) {
                fprintf(2, "rm: cannot open '%s': %d\n", path, fd);
            }
            return;
        }

        // 保存原始路径长度
        int original_len = strlen(path);

        // 确保路径以 '/' 结尾
        if (original_len > 0 && original_len < MAXPATHLEN &&
            path[original_len - 1] != '/') {
            path[original_len] = '/';
            original_len++;
        }

        // 读取目录项并递归删除
        while ((n = readn(fd, &f, sizeof(f))) == sizeof(f)) {
            if (f.f_name[0]) {
                int i = 0;
                int current_len = original_len;

                // 在原路径后追加文件名
                while (current_len < MAXPATHLEN - 1 && f.f_name[i]) {
                    path[current_len++] = f.f_name[i++];
                }

                if (current_len >= MAXPATHLEN - 1) {
                    path[original_len] = '\0';
                    fprintf(2, "rm: path too long for '%s/%s'\n", path,
                            f.f_name);
                    continue;
                }
                path[current_len] = '\0';

                remove_file(path, recursive, force);

                // 恢复原始路径
                path[original_len] = '\0';
            }
        }
        if (n < 0) {
            fprintf(2, "rm: error reading directory '%s': %d\n", path, n);
        }
        if (n > 0) {
            fprintf(2, "rm: short read in directory '%s'\n", path);
        }

        close(fd);
    }

    // 删除文件或空目录
    if ((r = remove(path)) < 0 && !force) {
        fprintf(2, "rm: cannot remove '%s': %d\n", path, r);
    }
    DEBUGF("rm: removed '%s'\n", path);
}

void usage(void) {
    fprintf(2, "usage: rm [-rf] [file...]\n");
    exit();
}

int main(int argc, char **argv) {
    int recursive = 0;
    int force = 0;
    char path_buffer[MAXPATHLEN];

    ARGBEGIN {
        default:
            usage();
        case 'r':
            recursive = 1;
            break;
        case 'f':
            force = 1;
            break;
    }
    ARGEND

    if (argc == 0) {
        usage();
    }

    for (int i = 0; i < argc; i++) {
        strcpy(path_buffer, argv[i]);
        remove_file(path_buffer, recursive, force);
    }

    return 0;
}