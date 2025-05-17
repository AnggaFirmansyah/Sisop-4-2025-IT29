#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_CHUNK_SIZE 1024
#define RELIC_DIR "/home/oryza/Modul_4/soal_2/relics"
#define LOG_FILE "/home/oryza/Modul_4/soal_2/activity.log"

void log_activity(const char *message) {
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", t);

    fprintf(log, "%s %s\n", timebuf, message);
    fclose(log);
}

static int baymax_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    const char *filename = path + 1;
    if (filename[0] == '\0') return -ENOENT;

    off_t size = 0;
    char chunk_path[512];
    int found = 0;

    for (int i = 0;; i++) {
        snprintf(chunk_path, sizeof(chunk_path), "%s/%s.%03d", RELIC_DIR, filename, i);
        FILE *f = fopen(chunk_path, "rb");
        if (!f) break;
        found = 1;
        fseek(f, 0, SEEK_END);
        size += ftell(f);
        fclose(f);
    }

    if (!found) return -ENOENT;

    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1;
    stbuf->st_size = size;
    return 0;
}

static int baymax_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset; (void)fi; (void)flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    DIR *dp = opendir(RELIC_DIR);
    if (!dp) return -ENOENT;

    struct dirent *entry;
    char prev_filename[512] = "";

    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char fname[512];
        strcpy(fname, entry->d_name);

        char *ext = strrchr(fname, '.');
        if (ext && strlen(ext) == 4) {
            *ext = '\0';
        }

        if (strcmp(prev_filename, fname) != 0) {
            filler(buf, fname, NULL, 0, 0);
            strcpy(prev_filename, fname);
        }
    }
    closedir(dp);
    return 0;
}

static int baymax_open(const char *path, struct fuse_file_info *fi) {
    const char *filename = path + 1;
    if (filename[0] == '\0')
        return -ENOENT;

    char chunk_path[512];
    snprintf(chunk_path, sizeof(chunk_path), "%s/%s.000", RELIC_DIR, filename);

    if (access(chunk_path, F_OK) == 0) {
        char log_msg[512];
        snprintf(log_msg, sizeof(log_msg), "READ: %s", filename);
        log_activity(log_msg);
    }

    return 0;
}

static int baymax_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
    const char *filename = path + 1;

    size_t total = 0;
    off_t current_offset = 0;
    char chunk_path[512], tmp_buf[MAX_CHUNK_SIZE];

    for (int i = 0;; i++) {
        snprintf(chunk_path, sizeof(chunk_path), "%s/%s.%03d", RELIC_DIR, filename, i);
        FILE *f = fopen(chunk_path, "rb");
        if (!f) break;

        size_t chunk_size = fread(tmp_buf, 1, MAX_CHUNK_SIZE, f);
        fclose(f);

        if (offset < current_offset + chunk_size) {
            size_t start = offset > current_offset ? offset - current_offset : 0;
            size_t to_copy = chunk_size - start;
            if (to_copy > size - total)
                to_copy = size - total;

            memcpy(buf + total, tmp_buf + start, to_copy);
            total += to_copy;
            offset += to_copy;

            if (total >= size) break;
        }

        current_offset += chunk_size;
    }

    return total;
}

static int baymax_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)mode;
    const char *filename = path + 1;

    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/baymax_%s_tmp", filename);

    FILE *tmp = fopen(tmpfile, "wb");
    if (!tmp) return -EIO;
    fclose(tmp);

    return 0;
}

static int baymax_write(const char *path, const char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    const char *filename = path + 1;

    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/baymax_%s_tmp", filename);

    FILE *tmp = fopen(tmpfile, "r+b");
    if (!tmp) tmp = fopen(tmpfile, "wb");
    if (!tmp) return -EIO;

    fseek(tmp, offset, SEEK_SET);
    size_t written = fwrite(buf, 1, size, tmp);
    fclose(tmp);

    return written;
}

static int baymax_release(const char *path, struct fuse_file_info *fi) {
    const char *filename = path + 1;

    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/baymax_%s_tmp", filename);

    FILE *src = fopen(tmpfile, "rb");
    if (!src) return 0;

    char *buffer;
    fseek(src, 0, SEEK_END);
    long fsize = ftell(src);
    rewind(src);

    buffer = malloc(fsize);
    if (!buffer) {
        fclose(src);
        return -ENOMEM;
    }

    fread(buffer, 1, fsize, src);
    fclose(src);

    mkdir(RELIC_DIR, 0755);

    long part_size = fsize / 14;
    long remainder = fsize % 14;
    long offset = 0;

    for (int i = 0; i < 14; ++i) {
        char chunk_path[512];
        snprintf(chunk_path, sizeof(chunk_path), "%s/%s.%03d", RELIC_DIR, filename, i);

        long this_part = part_size + (i < remainder ? 1 : 0);
        FILE *out = fopen(chunk_path, "wb");
        if (out) {
            fwrite(buffer + offset, 1, this_part, out);
            fclose(out);
        }
        offset += this_part;
    }

    free(buffer);
    remove(tmpfile);

    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "WRITE: %s -> %s.000 - %s.013", filename, filename, filename);
    log_activity(log_msg);

    return 0;
}

static int baymax_unlink(const char *path) {
    const char *filename = path + 1;
    char chunk_path[512];
    int i = 0;
    int found = 0;

    while (1) {
        snprintf(chunk_path, sizeof(chunk_path), "%s/%s.%03d", RELIC_DIR, filename, i);
        if (access(chunk_path, F_OK) != 0)
            break;
        remove(chunk_path);
        found = 1;
        i++;
    }

    if (found) {
        char log_msg[512];
        snprintf(log_msg, sizeof(log_msg), "DELETE: %s -> %s.000 - %s.%03d", filename, filename, filename, i - 1);
        log_activity(log_msg);
        return 0;
    }

    return -ENOENT;
}

static struct fuse_operations baymax_oper = {
    .getattr = baymax_getattr,
    .readdir = baymax_readdir,
    .open = baymax_open,
    .read = baymax_read,
    .create = baymax_create,
    .write = baymax_write,
    .release = baymax_release,
    .unlink = baymax_unlink,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &baymax_oper, NULL);
}
