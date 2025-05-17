
#define FUSE_USE_VERSION 31
#define _FILE_OFFSET_BITS 64
#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <curl/curl.h>

#define BASE_DIR "source_dir"
#define IMAGE_DIR "image"
#define TEMP_ZIP "temp.zip"

typedef struct {
    unsigned char *data;
    size_t size;
} ZipData;

static ZipData zip_data = {NULL, 0};

static int hex_to_bin(const char *hex_str, unsigned char **bin_data, size_t *bin_len) {
    size_t len = strlen(hex_str);
    if (len % 2 != 0) {
        fprintf(stderr, "[ERROR] Hex string length is odd\n");
        return -1;
    }

    *bin_len = len / 2;
    *bin_data = malloc(*bin_len);
    if (!*bin_data) {
        perror("[ERROR] Memory allocation failed");
        return -1;
    }

    for (size_t i = 0; i < len; i += 2) {
        if (sscanf(hex_str + i, "%2hhx", &(*bin_data)[i/2]) != 1) {
            fprintf(stderr, "[ERROR] Invalid hex byte at position %zu\n", i);
            free(*bin_data);
            return -1;
        }
    }
    return 0;
}

static void log_conversion(const char *filename, const char *image_name) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d][%H:%M:%S", t);

    FILE *log = fopen("conversion.log", "a");
    if (log) {
        fprintf(log, "[%s]: Successfully converted hexadecimal text %s to %s\n",
                timestamp, filename, image_name);
        fclose(log);
    } else {
        perror("[ERROR] Failed to open log file");
    }
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    zip_data.data = realloc(zip_data.data, zip_data.size + realsize);
    memcpy(zip_data.data + zip_data.size, ptr, realsize);
    zip_data.size += realsize;
    return realsize;
}

static int download_and_extract() {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[ERROR] Failed to initialize CURL\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, 
        "https://drive.usercontent.google.com/download?id=1hi_GDdP51Kn2JJMw02WmCOxuc3qrXzh5");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[ERROR] CURL failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_easy_cleanup(curl);

    FILE *fp = fopen(TEMP_ZIP, "wb");
    if (!fp) {
        perror("[ERROR] Failed to open temp.zip");
        return -1;
    }
    fwrite(zip_data.data, 1, zip_data.size, fp);
    fclose(fp);
    free(zip_data.data);

    mkdir(BASE_DIR, 0755);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "unzip -o %s -d %s && rm %s", TEMP_ZIP, BASE_DIR, TEMP_ZIP);
    return system(cmd);
}

static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    char full_path[512];
    if (strcmp(path, "/") == 0 || strcmp(path, "/anomali") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    snprintf(full_path, sizeof(full_path), "%s/anomali%s", BASE_DIR, path);
    return lstat(full_path, st) == -1 ? -errno : 0;
}

static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi,
                     enum fuse_readdir_flags flags) {
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    if (strcmp(path, "/") == 0) {
        filler(buf, "anomali", NULL, 0, 0);
        return 0;
    }

    if (strcmp(path, "/anomali") == 0) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/anomali", BASE_DIR);

        DIR *dir = opendir(full_path);
        if (!dir) return -errno;

        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (filler(buf, de->d_name, NULL, 0, 0)) break;
        }
        closedir(dir);
        return 0;
    }

    return -ENOENT;
}

static int do_open(const char *path, struct fuse_file_info *fi) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/anomali%s", BASE_DIR, path);

    int fd = open(full_path, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int do_read(const char *path, char *buf, size_t size, off_t offset,
                  struct fuse_file_info *fi) {
    fprintf(stderr, "[DEBUG] Reading: %s\n", path); // Debug message

    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) return -errno;

    if (offset == 0 && strstr(path, ".txt")) {
        fprintf(stderr, "[DEBUG] Processing .txt file: %s\n", path);

        struct stat st;
        if (fstat(fi->fh, &st) == -1) {
            perror("[ERROR] fstat failed");
            return res;
        }

        char *hex_str = malloc(st.st_size + 1);
        if (!hex_str) {
            perror("[ERROR] Memory allocation failed");
            return res;
        }

        ssize_t bytes_read = pread(fi->fh, hex_str, st.st_size, 0);
        if (bytes_read != st.st_size) {
            fprintf(stderr, "[ERROR] Failed to read file content\n");
            free(hex_str);
            return res;
        }
        hex_str[st.st_size] = '\0';

        unsigned char *bin_data;
        size_t bin_len;
        if (hex_to_bin(hex_str, &bin_data, &bin_len) == 0) {
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char timestamp[20];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H:%M:%S", t);

            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;

            if (mkdir(IMAGE_DIR, 0755) == -1 && errno != EEXIST) {
                perror("[ERROR] Failed to create image directory");
                free(bin_data);
                free(hex_str);
                return res;
            }

            char img_name[256];
            snprintf(img_name, sizeof(img_name), "%.200s_image_%s.png", base, timestamp);

            char img_path[512];
            snprintf(img_path, sizeof(img_path), "%s/%s", IMAGE_DIR, img_name);

            FILE *img = fopen(img_path, "wb");
            if (img) {
                fwrite(bin_data, 1, bin_len, img);
                fclose(img);
                log_conversion(base, img_name);
            } else {
                perror("[ERROR] Failed to create image file");
            }
            free(bin_data);
        }
        free(hex_str);
    }
    return res;
}

static struct fuse_operations ops = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .open = do_open,
    .read = do_read,
};

int main(int argc, char *argv[]) {
    if (download_and_extract() != 0) {
        fprintf(stderr, "[FATAL] Failed to initialize data\n");
        return 1;
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    fuse_opt_add_arg(&args, "-omax_threads=64");
    fuse_opt_add_arg(&args, "-odefault_permissions");

    int ret = fuse_main(args.argc, args.argv, &ops, NULL);
    fuse_opt_free_args(&args);
    return ret;
}
