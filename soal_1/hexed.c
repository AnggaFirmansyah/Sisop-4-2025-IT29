#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <curl/curl.h>

static char *g_hex_files_temp_path = NULL;
static char *g_current_working_dir = NULL;
static char g_temp_base_directory[1024];

size_t hex_to_bin(const char *hs, unsigned char *bb) {
    size_t sl = strlen(hs), hi = 0, bi = 0;
    while (hi < sl) {
        if (hs[hi]==' '||hs[hi]=='\n'||hs[hi]=='\r'||hs[hi]=='\t') { hi++; continue; }
        if (hi+1 >= sl) { fprintf(stderr, "Err: Odd hex length: %s\n", hs); return 0; }
        char bs[3]; bs[0]=hs[hi]; bs[1]=hs[hi+1]; bs[2]='\0';
        long val = strtol(bs, NULL, 16);
        if (errno == EINVAL || errno == ERANGE || (val == 0 && (bs[0]!='0'||bs[1]!='0'))) {
             fprintf(stderr, "Err: Invalid hex char: '%s'\n", bs); return 0;
        }
        bb[bi++]=(unsigned char)val; hi+=2;
    }
    return bi;
}

void log_conversion(const char *hf, const char *imf) {
    char lp[1024]; snprintf(lp, sizeof(lp), "%s/conversion.log", g_current_working_dir);
    FILE *lf = fopen(lp, "a");
    if (!lf) { perror("Err opening log"); return; }
    time_t rt; struct tm *ti; char ts[20];
    time(&rt); ti = localtime(&rt);
    strftime(ts, sizeof(ts), "%Y-%m-%d][%H:%M:%S", ti);
    fprintf(lf, "[%s]: Converted %s to %s.\n", ts, hf, imf);
    fclose(lf);
}

static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

static int shorekeeper_getattr(const char *p, struct stat *s, struct fuse_file_info *fi) {
    (void)fi; memset(s, 0, sizeof(struct stat));
    if (strcmp(p, "/") == 0) { s->st_mode = S_IFDIR | 0755; s->st_nlink = 2; }
    else if (strcmp(p, "/conversion.log") == 0) {
        char rlp[1024]; snprintf(rlp, sizeof(rlp), "%s/conversion.log", g_current_working_dir);
        if (lstat(rlp, s) == -1) return -errno;
    } else if (strstr(p, ".txt")) { s->st_mode = S_IFREG | 0444; s->st_nlink = 1; s->st_size = 4096; }
    else { return -ENOENT; }
    return 0;
}

static int shorekeeper_readdir(const char *p, void *b, fuse_fill_dir_t f, off_t o, struct fuse_file_info *fi, enum fuse_readdir_flags fl) {
    (void)o; (void)fi; (void)fl;
    if (strcmp(p, "/") == 0) {
        f(b, ".", NULL, 0, 0); f(b, "..", NULL, 0, 0); f(b, "conversion.log", NULL, 0, 0);
        DIR *dp = opendir(g_hex_files_temp_path);
        if (!dp) { fprintf(stderr, "Err opening hex dir: %s\n", g_hex_files_temp_path); return 0; }
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 && strstr(de->d_name, ".txt")) {
                f(b, de->d_name, NULL, 0, 0);
            }
        }
        closedir(dp);
    } else { return -ENOENT; }
    return 0;
}

static int shorekeeper_open(const char *p, struct fuse_file_info *fi) {
    if (strcmp(p, "/conversion.log") == 0) {
        char rlp[1024]; snprintf(rlp, sizeof(rlp), "%s/conversion.log", g_current_working_dir);
        int fd = open(rlp, O_RDONLY); if (fd == -1) return -errno;
        fi->fh = fd; return 0;
    } else if (strstr(p, ".txt")) { fi->fh = (uint64_t)-1; return 0; }
    else { return -ENOENT; }
}

static int shorekeeper_read(const char *p, char *b, size_t s, off_t o, struct fuse_file_info *fi) {
    if (strcmp(p, "/conversion.log") == 0) { return pread((int)fi->fh, b, s, o); }
    else if (strstr(p, ".txt")) {
        char hff[256]; strncpy(hff, p+1, sizeof(hff)-1); hff[sizeof(hff)-1]='\0';
        char *dot = strchr(hff, '.'); if (!dot) return -ENOENT;
        char hfb[256]; strncpy(hfb, hff, dot-hff); hfb[dot-hff]='\0';

        char ft_path[1024]; snprintf(ft_path, sizeof(ft_path), "%s/%s", g_hex_files_temp_path, hff);
        FILE *hf = fopen(ft_path, "r"); if (!hf) { perror("Err opening hex file"); return -ENOENT; }
        fseek(hf, 0, SEEK_END); long fs = ftell(hf); fseek(hf, 0, SEEK_SET);
        char *hsc = malloc(fs+1);
        if (!hsc) { fclose(hf); return -ENOMEM; }
        size_t hrl = fread(hsc, 1, fs, hf); hsc[hrl]='\0'; fclose(hf);

        unsigned char *id = malloc(hrl/2+1);
        if (!id) { free(hsc); return -ENOMEM; }
        size_t idl = hex_to_bin(hsc, id);
        free(hsc); if (idl == 0) { free(id); return -EIO; }

        char idp[1024]; snprintf(idp, sizeof(idp), "%s/image", g_current_working_dir); mkdir(idp, 0755);

        time_t rt; struct tm *ti; char ts[20];
        time(&rt); ti = localtime(&rt); strftime(ts, sizeof(ts), "%Y-%m-%d_%H:%M:%S", ti);
        char iofn[512]; snprintf(iofn, sizeof(iofn), "%s/%s_image_%s.png", idp, hfb, ts);

        FILE *imf = fopen(iofn, "wb");
        if (!imf) { perror("Err creating image file"); free(id); return -EIO; }
        fwrite(id, 1, idl, imf); fclose(imf);
        log_conversion(hff, iofn); free(id); return 0;
    } else { return -ENOENT; }
}

static int shorekeeper_release(const char *p, struct fuse_file_info *fi) {
    if (fi->fh != 0 && fi->fh != (uint64_t)-1) close((int)fi->fh);
    return 0;
}

static struct fuse_operations shorekeeper_oper = {
    .getattr=shorekeeper_getattr, .readdir=shorekeeper_readdir,
    .open=shorekeeper_open, .read=shorekeeper_read, .release=shorekeeper_release,
};

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <mount_point>\n", argv[0]); return 1; }

    g_current_working_dir = getcwd(NULL, 0);
    if (!g_current_working_dir) { perror("Err getting CWD"); return 1; }

    char temp_dir_template[] = "/tmp/shorekeeper_hex_XXXXXX";
    char *td_ptr = mkdtemp(temp_dir_template);
    if (!td_ptr) { perror("Err creating temp dir"); free(g_current_working_dir); return 1; }
    strncpy(g_temp_base_directory, temp_dir_template, sizeof(g_temp_base_directory)-1); g_temp_base_directory[sizeof(g_temp_base_directory)-1]='\0';

    CURL *ch; FILE *zip_fp; CURLcode cr;
    char *dl_url = "https://drive.usercontent.google.com/download?id=1hi_GDdP51Kn2JJMw02WmCOxuc3qrXzh5&export=download&authuser=0";
    char oz_fn[1024]; snprintf(oz_fn, sizeof(oz_fn), "%s/hex_anomalies.zip", g_temp_base_directory);

    curl_global_init(CURL_GLOBAL_DEFAULT); ch = curl_easy_init();
    if (ch) {
        zip_fp = fopen(oz_fn, "wb");
        if (!zip_fp) { perror("Err creating zip file"); curl_easy_cleanup(ch); curl_global_cleanup(); free(g_current_working_dir); rmdir(g_temp_base_directory); return 1; }
        curl_easy_setopt(ch, CURLOPT_URL, dl_url); curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, write_data); curl_easy_setopt(ch, CURLOPT_WRITEDATA, zip_fp);
        curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
        cr = curl_easy_perform(ch); fclose(zip_fp);
        if (cr != CURLE_OK) { fprintf(stderr, "Err: curl_easy_perform() failed: %s\n", curl_easy_strerror(cr)); remove(oz_fn); curl_easy_cleanup(ch); curl_global_cleanup(); free(g_current_working_dir); rmdir(g_temp_base_directory); return 1; }
        curl_easy_cleanup(ch);
    }
    curl_global_cleanup();

    char uz_cmd[2048]; snprintf(uz_cmd, sizeof(uz_cmd), "unzip -o %s -d %s", oz_fn, g_temp_base_directory);
    int uz_status = system(uz_cmd);
    if (uz_status != 0) { fprintf(stderr, "Err unzipping. Status: %d\n", uz_status); remove(oz_fn); free(g_current_working_dir); rmdir(g_temp_base_directory); return 1; }
    remove(oz_fn);

    char an_path[1024]; snprintf(an_path, sizeof(an_path), "%s/anomali", g_temp_base_directory);
    g_hex_files_temp_path = strdup(an_path); if (!g_hex_files_temp_path) { perror("Err allocating hex path"); free(g_current_working_dir); return 1; }

    char img_dir_path[1024]; snprintf(img_dir_path, sizeof(img_dir_path), "%s/image", g_current_working_dir);
    mkdir(img_dir_path, 0755);

    char log_path[1024]; snprintf(log_path, sizeof(log_path), "%s/conversion.log", g_current_working_dir);
    FILE *temp_log_f = fopen(log_path, "a"); if (temp_log_f) fclose(temp_log_f);

    char *fuse_argv[4] = {argv[0], argv[1], "-f", "-s"};
    int fs = fuse_main(4, fuse_argv, &shorekeeper_oper, NULL);

    DIR *anom_dp = opendir(g_hex_files_temp_path);
    if (anom_dp) {
        struct dirent *entry;
        while ((entry = readdir(anom_dp)) != NULL) {
            if (strcmp(entry->d_name, ".")==0||strcmp(entry->d_name, "..")==0) continue;
            char entry_fp[1024]; snprintf(entry_fp, sizeof(entry_fp), "%s/%s", g_hex_files_temp_path, entry->d_name);
            remove(entry_fp);
        }
        closedir(anom_dp);
    }
    rmdir(g_hex_files_temp_path);
    rmdir(g_temp_base_directory);

    free(g_hex_files_temp_path); free(g_current_working_dir);
    return fs;
}

