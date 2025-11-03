#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

static void sendall(int c, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len) {
        ssize_t n = send(c, p, len, 0);
        if (n <= 0) return;
        p += n;
        len -= (size_t)n;
    }
}

static void send_text(int c, int code, const char *reason, const char *body) {
    char h[256];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 %d %s\r\n"
                     "Content-Type: text/plain; charset=UTF-8\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     code, reason, strlen(body));
    sendall(c, h, (size_t)n);
    sendall(c, body, strlen(body));
}

static const char *mime_type_for(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) return "text/html; charset=UTF-8";
    if (!strcasecmp(ext, "txt")) return "text/plain; charset=UTF-8";
    if (!strcasecmp(ext, "css")) return "text/css; charset=UTF-8";
    if (!strcasecmp(ext, "js")) return "application/javascript";
    if (!strcasecmp(ext, "json")) return "application/json";
    if (!strcasecmp(ext, "png")) return "image/png";
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, "gif")) return "image/gif";
    if (!strcasecmp(ext, "svg")) return "image/svg+xml";
    if (!strcasecmp(ext, "pdf")) return "application/pdf";
    return "application/octet-stream";
}

static int safe_path(const char *user_path, char *resolved, size_t size) {
    if (!user_path || !*user_path) return snprintf(resolved, size, ".") < (int)size;
    if (strstr(user_path, "..") || user_path[0] == '/') return 0;

    char tmp[PATH_MAX];
    size_t len = 0;
    for (size_t i = 0; user_path[i] && len < sizeof(tmp) - 1; i++) {
        if (user_path[i] == '/' && (i == 0 || user_path[i - 1] == '/')) continue;
        tmp[len++] = user_path[i];
    }
    tmp[len] = '\0';

    return snprintf(resolved, size, "%s", tmp[0] ? tmp : ".") < (int)size;
}

static void send_file(int c, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_text(c, 404, "Not Found", "404\n");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        send_text(c, 403, "Forbidden", "403\n");
        return;
    }

    const char *mime = mime_type_for(path);
    char h[512];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %ld\r\n"
                     "Connection: close\r\n\r\n",
                     mime, (long)st.st_size);
    sendall(c, h, (size_t)n);

    char b[8192];
    size_t r;
    while ((r = fread(b, 1, sizeof(b), f)) > 0) {
        sendall(c, b, r);
    }
    fclose(f);
}

void list_dir_stream(int c, const char *rel) {
    // Cabeçalho HTTP
    const char *hdr =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "X-Dir-Listing: 1\r\n"
        "Connection: close\r\n\r\n";
    sendall(c, hdr, strlen(hdr));

    // Abre diretório
    DIR *d = opendir((rel && *rel) ? rel : ".");
    if (!d) {
        send_text(c, 403, "Forbidden", "403\n");
        return;
    }

    // Início do HTML
    char buf[8192];
    int n = snprintf(buf, sizeof(buf),
        "<html><head><title>Itens do diretorio</title></head>"
        "<body><h2>Itens do diretorio</h2><ul>");
    sendall(c, buf, (size_t)n);

    // Lista arquivos
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        n = snprintf(buf, sizeof(buf),
                     "<li><a href=\"%s/%s\">%s</a></li>",
                     rel ? rel : ".", e->d_name, e->d_name);
        sendall(c, buf, (size_t)n);
    }

    closedir(d);

    // Fecha HTML
    n = snprintf(buf, sizeof(buf), "</ul></body></html>");
    sendall(c, buf, (size_t)n);
    close(c);
}

static void handle(int c) {
    char req[4096];
    ssize_t r = recv(c, req, sizeof(req) - 1, 0);
    if (r <= 0) { close(c); return; }
    req[r] = '\0';

    char m[8], p_raw[2048];
    if (sscanf(req, "%7s %2047s ", m, p_raw) != 2 || strcmp(m, "GET") != 0) {
        send_text(c, 405, "Method Not Allowed", "405\n");
        close(c);
        return;
    }

    if (p_raw[0] == '/') memmove(p_raw, p_raw + 1, strlen(p_raw));
    char *q = strchr(p_raw, '?');
    if (q) *q = '\0';

    char p_safe[PATH_MAX];
    if (!safe_path(p_raw, p_safe, sizeof(p_safe))) {
        send_text(c, 403, "Forbidden", "403\n");
        close(c);
        return;
    }

    if (!*p_safe) {
        struct stat st;
        if (stat("index.html", &st) == 0 && S_ISREG(st.st_mode)) {
            send_file(c, "index.html");
        } else {
            list_dir_stream(c, ".");
        }
        close(c);
        return;
    }

    struct stat st;
    if (stat(p_safe, &st) == 0 && S_ISDIR(st.st_mode)) {
        char idx[PATH_MAX];
        snprintf(idx, sizeof(idx), "%s/index.html", p_safe);
        if (stat(idx, &st) == 0 && S_ISREG(st.st_mode)) {
            send_file(c, idx);
        } else {
            list_dir_stream(c, p_safe);
        }
        close(c);
        return;
    }

    send_file(c, p_safe);
    close(c);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: %s /dir [--port N]\n", argv[0]);
        return 1;
    }

    int port = 5050;
    if (argc >= 4 && strcmp(argv[2], "--port") == 0) {
        port = atoi(argv[3]);
    }

    if (chdir(argv[1]) != 0) {
        perror("chdir");
        return 1;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);

    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 16) < 0) {
        perror("bind/listen");
        return 1;
    }

    printf("Servidor em http://localhost:%d  servindo: %s\n", port, argv[1]);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(s, (struct sockaddr *)&ca, &cl);
        if (c < 0) continue;
        handle(c);
    }
}