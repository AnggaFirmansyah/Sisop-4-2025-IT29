#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>

const char *dirpath = "/it24_host";
const char *logpath = "/var/log/it24.log";

int is_dangerous(const char *filename) {
    char lower[512];
    strncpy(lower, filename, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    for (int i = 0; lower[i]; i++) lower[i] = tolower(lower[i]);
    return strstr(lower, "nafis") || strstr(lower, "kimcun");
}

void strrev(char *str) {
    int len = strlen(str), i;
    for (i = 0; i < len / 2; i++) {
        char tmp = str[i];
        str[i] = str[len - i - 1];
        str[len - i - 1] = tmp;
    }
}

void rot13(char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if ((buf[i] >= 'a' && buf[i] <= 'z'))
            buf[i] = 'a' + (buf[i] - 'a' + 13) % 26;
        else if ((buf[i] >= 'A' && buf[i] <= 'Z'))
            buf[i] = 'A' + (buf[i] - 'A' + 13) % 26;
    }
}

void log_action(const char *type, const char *msg) {
    FILE *f = fopen(logpath, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        type, msg);
    fclose(f);
}

static int xmp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    char fpath[1024];
    snprintf(fpath, sizeof(fpath), "%s%s", dirpath, path);
    return lstat(fpath, stbuf);
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    DIR *dp;
    struct dirent *de;
    char fpath[1024];
    snprintf(fpath, sizeof(fpath), "%s%s", dirpath, path);

    dp = opendir(fpath);
    if (dp == NULL) return -errno;

    while ((de = readdir(dp)) != NULL) {
        char fullfile[2048];
        snprintf(fullfile, sizeof(fullfile), "%s/%s", fpath, de->d_name);

        struct stat st;
        if (stat(fullfile, &st) != 0) {
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
        }

        char name[512];
        strncpy(name, de->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        if (is_dangerous(name)) {
            char rev[512];
            strncpy(rev, name, sizeof(rev) - 1);
            rev[sizeof(rev) - 1] = '\0';
            strrev(rev);
            filler(buf, rev, &st, 0, 0);

            char logmsg[2048];
            snprintf(logmsg, sizeof(logmsg), "File %s has been reversed : %s", name, rev);
            log_action("REVERSE", logmsg);
        } else {
            filler(buf, name, &st, 0, 0);
        }
    }
    closedir(dp);
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi) {
    char fpath[1024];
    snprintf(fpath, sizeof(fpath), "%s%s", dirpath, path);

    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    fi->fh = fd;

    if (is_dangerous(path)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Anomaly detected nafis in file: %s", path);
        log_action("ALERT", msg);
    }

    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int fd = fi->fh;
    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;

    if (!is_dangerous(path) && strstr(path, ".txt")) {
        rot13(buf, res);
        char msg[512];
        snprintf(msg, sizeof(msg), "File %s has been encrypted", path);
        log_action("ENCRYPT", msg);
    }

    return res;
}

static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .open = xmp_open,
    .read = xmp_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &xmp_oper, NULL);
}

