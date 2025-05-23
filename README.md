# Sisop-4-2025-IT29

- Bayu Kurniawan - 5027241055
- Angga Firmansyah - 5027241062
- Oryza Qiara Ramadhani - 5027241084

# Soal 1
Oleh : Angga Firmansyah (062)

The Shorekeeper adalah sebuah entitas misterius yang memimpin dan menjaga Black Shores secara keseluruhan. Karena Shorekeeper hanya berada di Black Shores, ia biasanya berjalan - jalan di sekitar Black Shores untuk mencari anomali - anomali yang ada untuk mencegah adanya kekacauan ataupun krisis di Black Shores. Semenjak kemunculan Fallacy of No Return, ia semakin ketat dalam melakukan pencarian anomali - anomali yang ada di Black Shores untuk mencegah hal yang sama terjadi lagi.
Suatu hari, saat di Tethys' Deep, Shorekeeper menemukan sebuah anomali yang baru diketahui. Anomali ini berupa sebuah teks acak yang kelihatannya tidak memiliki arti. Namun, ia mempunyai ide untuk mencari arti dari teks acak tersebut. [Author: Haidar / scar / hemorrhager / 恩赫勒夫]

a. Pertama, Shorekeeper akan mengambil beberapa sampel anomali teks dari link berikut. Pastikan file zip terhapus setelah proses unzip.

b. Setelah melihat teks - teks yang didapatkan, ia menyadari bahwa format teks tersebut adalah hexadecimal. Dengan informasi tersebut, Shorekeeper mencoba untuk mencoba idenya untuk mencari makna dari teks - teks acak tersebut, yaitu dengan mengubahnya dari string hexadecimal menjadi sebuah file image. Bantulah Shorekeeper dengan membuat kode untuk FUSE yang dapat mengubah string hexadecimal menjadi sebuah gambar ketika file text tersebut dibuka di mount directory. Lalu, letakkan hasil gambar yang didapat ke dalam directory bernama “image”.


c. Untuk penamaan file hasil konversi dari string ke image adalah [nama file string]_image_[YYYY-mm-dd]_[HH:MM:SS].

Contoh:
1_image_2025-05-11_18:35:26.png

d. Catat setiap konversi yang ada ke dalam sebuah log file bernama conversion.log. Untuk formatnya adalah sebagai berikut.

```bash
[YYYY-mm-dd][HH:MM:SS]: Successfully converted hexadecimal text [nama file string] to [nama file image].
```
Contoh :
```bash
[2025-05-11][18:35:26]: Successfully converted hexadecimal text 1.txt to 1_image_2025-05-11_18:35:26.png.
[2025-05-11][18:35:27]: Successfully converted hexadecimal text 2.txt to 2_image_2025-05-11_18:35:27.png.
[2025-05-11][18:35:29]: Successfully converted hexadecimal text 3.txt to 3_image_2025-05-11_18:35:29.png.
[2025-05-11][18:35:32]: Successfully converted hexadecimal text 4.txt to 4_image_2025-05-11_18:35:32.png.
[2025-05-11][18:35:34]: Successfully converted hexadecimal text 5.txt to 5_image_2025-05-11_18:35:34.png.
[2025-05-11][18:35:36]: Successfully converted hexadecimal text 6.txt to 6_image_2025-05-11_18:35:36.png.
[2025-05-11][18:35:38]: Successfully converted hexadecimal text 7.txt to 7_image_2025-05-11_18:35:38.png.
```

# Penyelesaian 

Kode penyelesaian : 

```bash
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

#define BASE_DIR "/home/khalid/a/source_dir"  // Ganti ini ke path absolut sesuai di PC kamu
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

// Debug fungsi untuk tes baca folder anomali
void test_read_anomali() {
    char real[512];
    snprintf(real, sizeof(real), "%s/anomali", BASE_DIR);
    DIR *d = opendir(real);
    if (!d) {
        perror("opendir test_read_anomali");
        return;
    }
    struct dirent *e;
    printf("Testing read dir %s\n", real);
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            printf("Found file: %s\n", e->d_name);
    }
    closedir(d);
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
        printf("Reading directory in do_readdir: %s\n", real); // debug

        DIR *d = opendir(real);
        if (!d) {
            perror("opendir do_readdir");
            return -errno;
        }
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                printf("Found file in do_readdir: %s\n", e->d_name); // debug
                filler(buf, e->d_name, NULL, 0, 0);
            }
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

    test_read_anomali();  // Debug: cek folder anomali dan isinya di source_dir

    return fuse_main(argc, argv, &ops, NULL);
}
```

Hanya bisa mendownload, unzip, serta menghapus zip.

Dokumentasi : 

![image](https://github.com/user-attachments/assets/bf0ab864-a3b2-480f-8635-9eef7a3bc591)

- Poin diatas menyelesaian poin a yang mendownload, unzip, delete zip. Proses mouunt fuse berhasil namun tidak terbuat direktori image serta conversion.log.
- Kode mendownload anomaly.zip yang di letakkan pada direktori mountpoint yang dibuat secara manual

![image](https://github.com/user-attachments/assets/360560ec-a291-4460-aef2-dc1e783a45ba)

- Berhasil melakukan konversi hex ke image
- Hasil konverso berhasil terkirim ke direktori image

# REVISI 

Dikarenakan kode sebelumnya hanya bisa menyelesaikan soal bagian a, maka perlu dibuat revisi untuk menyelesaikan seluruh soal. Berikut merupakan revisi dari kode diatas.


# Soal 2
Oleh : Oryza Qiara Ramadhani (084)

Program ini menggunakan FUSE (Filesystem in Userspace) untuk membuat direktori virtual (mount_dir) yang menampilkan file utuh seperti Baymax.jpeg, padahal aslinya tersimpan dalam pecahan-pecahan di folder relics. Setiap operasi penting seperti membaca, menulis, menghapus, dan menyalin file akan dicatat dalam log file activity.log, agar sang ilmuwan dapat melacak semua aktivitas yang dilakukan terhadap Baymax.

a. Menampilkan file utuh dari beberapa pecahan 
Ketika kita menjalankan ls atau membuka file di mount_dir, fungsi getattr ini akan dijalankan. Tujuannya adalah mengecek apakah file itu ada dengan menggabungkan ukuran seluruh pecahan.

```bash
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
```

b. Menampilkan Daftar File (readdir)
Di folder relics, setiap file terbagi dalam potongan .000, .001, dst. Tapi di mount FUSE, kita hanya ingin tampilkan nama utuhnya. Jadi readdir akan menyaring hanya nama dasarnya dan tidak menampilkan duplikat.
```bash
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
```

c. Membaca File Gabungan (open & read)
Saat file dibuka atau dibaca, program akan mencatat log dan menggabungkan isi semua pecahan menjadi satu aliran data.
```bash
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
```
```bash
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
```

d. Menulis dan Memecah File Baru (create, write, release)
Saat pengguna membuat file baru, kita menulisnya ke /tmp, lalu saat file ditutup (release), file dipecah jadi 14 bagian dan disimpan di relics.
```bash
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
```
```bash
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
```
```bash
static int baymax_release(const char *path, struct fuse_file_info *fi) {
    const char *filename = path + 1;

    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/baymax_%s_tmp", filename);

    FILE *src = fopen(tmpfile, "rb");
    if (!src) return 0;

    fseek(src, 0, SEEK_END);
    long fsize = ftell(src);
    rewind(src);

    char *buffer = malloc(fsize);
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
```

e. Menghapus File dan Semua Pecahannya (unlink)
Jika pengguna menghapus Baymax.jpeg di mount, maka semua file .000 sampai .013 harus ikut dihapus.
```bash
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
```
f. Pencatatan Aktivitas Selama Proses Dijalankan
```bash
[2025-05-19 14:19:11] WRITE: baru.txt -> baru.txt.000 - baru.txt.013
[2025-05-19 14:19:21] READ: baru.txt
[2025-05-19 14:20:13] DELETE: baru.txt -> baru.txt.000 - baru.txt.013
[2025-05-19 14:22:41] READ: Baymax.jpeg
[2025-05-20 16:21:49] READ: Baymax.jpeg
```

Dengan menjalankan beberapa command
- ``gcc baymax.c `pkg-config fuse3 --cflags --libs` -o baymax``
- ``./baymax mount_dir/``
- ``sudo umount mount_dir``

Maka Menghasilkan output seperti ini:
![image](https://github.com/user-attachments/assets/e9eabe37-0589-4780-85e6-bc35c17e2aea)

# Soal 3
Oleh : Oryza Qiara Ramadhani (084)

# Soal 4
Oleh : 
