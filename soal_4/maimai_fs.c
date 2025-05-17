#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define BACKEND_PATH "chiho/starter"
#define EXT ".mai"

static void add_extension(const char *path, char *fullpath) {
    sprintf(fullpath, "%s%s%s", BACKEND_PATH, path, EXT);
}

static void full_dir_path(const char *path, char *fullpath) {
    sprintf(fullpath, "%s%s", BACKEND_PATH, path);
}

static int starter_getattr(const char *path, struct stat *stbuf) {
    char real_path[1024];
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        full_dir_path(path, real_path);
    } else {
        add_extension(path, real_path);
    }

    int res = lstat(real_path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int starter_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi) {
    DIR *dp;
    struct dirent *de;
    char dir_path[1024];

    full_dir_path(path, dir_path);

    dp = opendir(dir_path);
    if (dp == NULL)
        return -errno;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    while ((de = readdir(dp)) != NULL) {
        if (de->d_type == DT_REG && strstr(de->d_name, EXT)) {
            char name[256];
            strncpy(name, de->d_name, strlen(de->d_name) - strlen(EXT));
            name[strlen(de->d_name) - strlen(EXT)] = '\0';
            filler(buf, name, NULL, 0);
        }
    }

    closedir(dp);
    return 0;
}

static int starter_open(const char *path, struct fuse_file_info *fi) {
    char real_path[1024];
    add_extension(path, real_path);

    int fd = open(real_path, fi->flags);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int starter_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    int res = pread(fi->fh, buf, size, offset);
    return (res == -1) ? -errno : res;
}

static int starter_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    int res = pwrite(fi->fh, buf, size, offset);
    return (res == -1) ? -errno : res;
}

static int starter_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char real_path[1024];
    add_extension(path, real_path);

    int fd = open(real_path, fi->flags | O_CREAT, mode);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int starter_unlink(const char *path) {
    char real_path[1024];
    add_extension(path, real_path);
    int res = unlink(real_path);
    return (res == -1) ? -errno : 0;
}

static int starter_release(const char *path, struct fuse_file_info *fi) {
    close(fi->fh);
    return 0;
}

static struct fuse_operations starter_oper = {
    .getattr = starter_getattr,
    .readdir = starter_readdir,
    .open    = starter_open,
    .read    = starter_read,
    .write   = starter_write,
    .create  = starter_create,
    .unlink  = starter_unlink,
    .release = starter_release,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &starter_oper, NULL);
}
