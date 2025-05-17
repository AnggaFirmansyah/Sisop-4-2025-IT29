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

// Struktur simpan data ZIP
typedef struct {
    unsigned char *data;
    size_t size;
} ZipData;

static ZipData zip_data = {NULL, 0};

// Fungsi hex ke binari
static int hex_to_bin(const char *hex_str, unsigned char **bin_data, size_t *bin_len) {
    size_t len = strlen(hex_str);
    if (len % 2 != 0) return -1;

    *bin_len = len / 2;
    *bin_data = malloc(*bin_len);
    if (!*bin_data) return -1;

    for (size_t i = 0; i < len; i += 2) {
        if (sscanf(hex_str + i, "%2hhx", &(*bin_data)[i / 2]) != 1) {
            free(*bin_data);
            return -1;
        }
    }
    return 0;
}

// Log konversi
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
    }
}

// Callback curl untuk tulis data
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    unsigned char *tmp = realloc(zip_data.data, zip_data.size + realsize);
    if (!tmp) {
        // realloc gagal
        return 0;
    }
    zip_data.data = tmp;
    memcpy(zip_data.data + zip_data.size, ptr, realsize);
    zip_data.size += realsize;
    return realsize;
}

// Download dan ekstrak ZIP
static int download_and_extract() {
    printf("[*] Downloading ZIP file...\n");
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL,
        "https://drive.usercontent.google.com/download?id=1hi_GDdP51Kn2JJMw02WmCOxuc3qrXzh5");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Curl download failed: %s\n", curl_easy_strerror(res));
        return -1;
    }

    FILE *fp = fopen(TEMP_ZIP, "wb");
    if (!fp) {
        perror("fopen temp.zip");
        free(zip_data.data);
        return -1;
    }
    fwrite(zip_data.data, 1, zip_data.size, fp);
    fclose(fp);
    free(zip_data.data);
    zip_data.data = NULL;
    zip_data.size = 0;

    printf("[*] Extracting ZIP file...\n");
    // Buat folder BASE_DIR jika belum ada
    struct stat st = {0};
    if (stat(BASE_DIR, &st) == -1) {
        if (mkdir(BASE_DIR, 0755) != 0) {
            perror("mkdir source_dir");
            return -1;
        }
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "unzip -o %s -d %s && rm %s", TEMP_ZIP, BASE_DIR, TEMP_ZIP);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Unzip command failed with code %d\n", ret);
        return -1;
    }

    printf("[*] Download and extract complete.\n");
    return 0;
}

// Fungsi bantu untuk gabung path dengan aman
static void join_path(char *dest, size_t size, const char *base, const char *path) {
    if (path[0] == '/') {
        snprintf(dest, size, "%s%s", base, path);
    } else {
        snprintf(dest, size, "%s/%s", base, path);
    }
}

// FUSE getattr
static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    (void)fi;
    memset(st, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0 || strcmp(path, "/anomali") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    char full_path[512];
    join_path(full_path, sizeof(full_path), BASE_DIR, path);

    int ret = lstat(full_path, st);
    if (ret == -1) {
        perror("lstat");
        return -errno;
    }
    return 0;
}

// FUSE readdir
static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    if (strcmp(path, "/") == 0) {
        filler(buf, "anomali", NULL, 0, 0);
        return 0;
    }

    char full_path[512];
    join_path(full_path, sizeof(full_path), BASE_DIR, path);

    DIR *dir = opendir(full_path);
    if (!dir) {
        perror("opendir");
        return -errno;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        filler(buf, de->d_name, NULL, 0, 0);
    }
    closedir(dir);
    return 0;
}

// FUSE open
static int do_open(const char *path, struct fuse_file_info *fi) {
    char full_path[512];
    join_path(full_path, sizeof(full_path), BASE_DIR, path);

    int fd = open(full_path, fi->flags);
    if (fd == -1) {
        perror("open");
        return -errno;
    }
    fi->fh = fd;
    return 0;
}

// FUSE read: baca dan convert jika .txt
static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) {
        perror("pread");
        return -errno;
    }

    // Hanya konversi di offset 0 dan jika file .txt
    if (offset == 0 && strstr(path, ".txt")) {
        struct stat st;
        if (fstat(fi->fh, &st) == -1) {
            perror("fstat");
            return res;
        }

        char *hex_str = malloc(st.st_size + 1);
        if (!hex_str) return res;

        ssize_t read_bytes = pread(fi->fh, hex_str, st.st_size, 0);
        if (read_bytes != st.st_size) {
            free(hex_str);
            return res;
        }
        hex_str[st.st_size] = '\0';

        unsigned char *bin_data;
        size_t bin_len;
        if (hex_to_bin(hex_str, &bin_data, &bin_len) == 0) {
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H:%M:%S", t);

            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;

            // Buat folder image jika belum ada
            struct stat stimg = {0};
            if (stat(IMAGE_DIR, &stimg) == -1) {
                mkdir(IMAGE_DIR, 0755);
            }

            char img_name[256];
            snprintf(img_name, sizeof(img_name), "%.*s_image_%s.png",
                     (int)(strlen(base) - 4), base, timestamp);

            char img_path[512];
            snprintf(img_path, sizeof(img_path), "%s/%s", IMAGE_DIR, img_name);

            FILE *img = fopen(img_path, "wb");
            if (img) {
                fwrite(bin_data, 1, bin_len, img);
                fclose(img);
                log_conversion(base, img_name);
                printf("[*] Converted %s to %s\n", base, img_name);
            } else {
                perror("fopen image");
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
        fprintf(stderr, "Gagal mengunduh atau mengekstrak ZIP!\n");
        return 1;
    }

    printf("[*] Starting FUSE...\n");
    return fuse_main(argc, argv, &ops, NULL);
}
