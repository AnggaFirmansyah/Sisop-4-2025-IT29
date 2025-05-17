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
#define TEMP_ZIP "/tmp/temp.zip"
#define IMAGE_DIR "image"

typedef struct {
    unsigned char *data;
    size_t size;
} Memory;
static Memory zip_data = {NULL, 0};

// Curl write callback (to memory)
static size_t write_to_memory(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    Memory *mem = (Memory *)userdata;
    unsigned char *new_ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!new_ptr) return 0;
    mem->data = new_ptr;
    memcpy(&(mem->data[mem->size]), ptr, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}

// Download file ZIP dari Google Drive dan extract
static int download_and_extract() {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    const char *file_id = "1hi_GDdP51Kn2JJMw02WmCOxuc3qrXzh5";
    const char *base_url = "https://drive.google.com/uc?export=download&id=1hi_GDdP51Kn2JJMw02WmCOxuc3qrXzh5";
    char confirm_token[128] = {0};
    char cookie_file[] = "/tmp/cookies.txt";
    char url[512];

    // Step 1: Fetch token page
    Memory page = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, base_url);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file);
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &page);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Gagal download halaman awal: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }

    char *p = NULL;
    if (page.data) {
        p = strstr((char *)page.data, "confirm=");
        if (p) sscanf(p, "confirm=%127[^&\"]", confirm_token);
    }
    free(page.data);

    // Step 2: Download ZIP file
    if (strlen(confirm_token) > 0) {
        snprintf(url, sizeof(url),
            "https://drive.google.com/uc?export=download&confirm=%s&id=%s",
            confirm_token, file_id);
    } else {
        snprintf(url, sizeof(url), "%s", base_url);
    }

    zip_data.data = NULL;
    zip_data.size = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &zip_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Gagal download ZIP: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);

    FILE *fp = fopen(TEMP_ZIP, "wb");
    if (!fp) {
        perror("fopen temp.zip");
        free(zip_data.data);
        return -1;
    }
    fwrite(zip_data.data, 1, zip_data.size, fp);
    fclose(fp);
    free(zip_data.data);

    mkdir(BASE_DIR, 0755);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "unzip -o %s -d %s && rm %s", TEMP_ZIP, BASE_DIR, TEMP_ZIP);
    return system(cmd);
}

// Konversi hex string ke biner
static int hex_to_bin(const char *hex, unsigned char **out, size_t *outlen) {
    size_t len = strlen(hex);
    if (len % 2 != 0) return -1;
    *outlen = len / 2;
    *out = malloc(*outlen);
    if (!*out) return -1;
    for (size_t i = 0; i < len; i += 2) {
        if (sscanf(hex + i, "%2hhx", &(*out)[i / 2]) != 1) {
            free(*out);
            return -1;
        }
    }
    return 0;
}

// Log hasil konversi ke file
static void log_conversion(const char *src, const char *out) {
    FILE *f = fopen("conversion.log", "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d][%H:%M:%S", tm);
    fprintf(f, "[%s]: Successfully converted hexadecimal text %s to %s\n", ts, src, out);
    fclose(f);
}

// ========== FUSE Operations ==========

static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    (void)fi;
    memset(st, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0 || strcmp(path, "/anomali") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    // Tangani file dalam /anomali
    if (strncmp(path, "/anomali/", 9) == 0) {
        char full[512];
        snprintf(full, sizeof(full), "%s%s", BASE_DIR, path);
        if (lstat(full, st) == -1) {
            perror("getattr lstat");
            return -errno;
        }
        return 0;
    }

    return -ENOENT;
}

static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset; (void)fi; (void)flags;
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    if (strcmp(path, "/") == 0) {
        filler(buf, "anomali", NULL, 0, 0);
        return 0;
    }

    if (strcmp(path, "/anomali") == 0) {
        char real[512];
        snprintf(real, sizeof(real), "%s/anomali", BASE_DIR);
        DIR *d = opendir(real);
        if (!d) return -errno;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                filler(buf, e->d_name, NULL, 0, 0);
        }
        closedir(d);
        return 0;
    }

    return -ENOENT;
}

static int do_open(const char *path, struct fuse_file_info *fi) {
    char full[512];
    snprintf(full, sizeof(full), "%s%s", BASE_DIR, path);
    int fd = open(full, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) return -errno;

    if (offset == 0 && strstr(path, ".txt")) {
        struct stat st;
        if (fstat(fi->fh, &st) == -1) return res;

        char *hex = malloc(st.st_size + 1);
        if (!hex) return res;
        pread(fi->fh, hex, st.st_size, 0);
        hex[st.st_size] = '\0';

        unsigned char *bin;
        size_t bin_len;
        if (hex_to_bin(hex, &bin, &bin_len) == 0) {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            char ts[64];
            strftime(ts, sizeof(ts), "%Y-%m-%d_%H:%M:%S", tm);

            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;

            mkdir(IMAGE_DIR, 0755);

            char name[256];
            snprintf(name, sizeof(name), "%.*s_image_%s.png",
                     (int)(strlen(base) - 4), base, ts);
            char outpath[512];
            snprintf(outpath, sizeof(outpath), "%s/%s", IMAGE_DIR, name);

            FILE *f = fopen(outpath, "wb");
            if (f) {
                fwrite(bin, 1, bin_len, f);
                fclose(f);
                log_conversion(base, name);
            }
            free(bin);
        }
        free(hex);
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
        fprintf(stderr, "Gagal download atau extract!\n");
        return 1;
    }
    return fuse_main(argc, argv, &ops, NULL);
}
