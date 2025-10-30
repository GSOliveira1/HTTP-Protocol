#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>

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
    char h[256];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Type: application/octet-stream\r\n"
                     "Content-Length: %ld\r\n"
                     "Connection: close\r\n\r\n",
                     (long)st.st_size);
    sendall(c, h, (size_t)n);
    char b[8192];
    size_t r;
    while ((r = fread(b, 1, sizeof(b), f)) > 0) {
        sendall(c, b, r);
    }
    fclose(f);
}

static void list_dir_stream(int c, const char *rel) {
    const char *hdr =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "X-Dir-Listing: 1\r\n"
        "Connection: close\r\n\r\n";
    sendall(c, hdr, strlen(hdr));

    DIR *d = opendir(*rel ? rel : ".");
    if (!d) return;

    struct dirent *e;
    char line[4096];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        int n = snprintf(line, sizeof(line), "%s\n", e->d_name);
        sendall(c, line, (size_t)n);
    }
    closedir(d);
}

static void handle(int c) {
    char req[4096];
    ssize_t r = recv(c, req, sizeof(req) - 1, 0);
    if (r <= 0) { close(c); return; }
    req[r] = '\0';

    char m[8], p[2048];
    if (sscanf(req, "%7s %2047s ", m, p) != 2 || strcmp(m, "GET") != 0) {
        send_text(c, 405, "Method Not Allowed", "405\n");
        close(c);
        return;
    }

    if (p[0] == '/') memmove(p, p + 1, strlen(p));
    char *q = strchr(p, '?');
    if (q) *q = '\0';

    if (strstr(p, "..")) {
        send_text(c, 403, "Forbidden", "403\n");
        close(c);
        return;
    }

    if (!*p) {
        struct stat st;
        if (stat("index.html", &st) == 0 && S_ISREG(st.st_mode)) {
            send_file(c, "index.html");
        } else {
            list_dir_stream(c, "");
        }
        close(c);
        return;
    }

    struct stat st;
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
        char idx[2200];
        snprintf(idx, sizeof(idx), "%s/index.html", p);
        if (stat(idx, &st) == 0 && S_ISREG(st.st_mode)) {
            send_file(c, idx);
        } else {
            list_dir_stream(c, p);
        }
        close(c);
        return;
    }

    send_file(c, p);
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