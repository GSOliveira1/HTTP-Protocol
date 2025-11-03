#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

int main(int argc, char **argv) {
    if (argc != 2 || strncmp(argv[1], "http://", 7) != 0) {
        fprintf(stderr, "uso: %s http://host[:porta]/path\n", argv[0]);
        return 1;
    }

    char host[512] = {0};
    char path[1024] = "/";
    char file[256] = "index.html";
    int port = 80;

    const char *p = argv[1] + 7;
    const char *slash = strchr(p, '/');
    size_t host_len = slash ? (size_t)(slash - p) : strlen(p);
    if (host_len >= sizeof(host)) return 1;
    memcpy(host, p, host_len);
    if (slash) {
        strncpy(path, slash, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    char *col = strchr(host, ':');
    if (col) {
        *col = '\0';
        port = atoi(col + 1);
        if (port <= 0 || port > 65535) return 1;
    }

    const char *fname = strrchr(path, '/');
    if (fname && *(fname + 1)) {
        snprintf(file, sizeof(file), "%s", fname + 1);
    }

    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return 1;

    int s = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == -1) continue;
        if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(s);
        s = -1;
    }
    freeaddrinfo(res);
    if (s == -1) return 1;

    char req[1024];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.0\r\n"
                     "Host: %s\r\n"
                     "\r\n",
                     path, host);
    if (send(s, req, n, 0) < 0) {
        close(s);
        return 1;
    }

    FILE *fp = NULL;
    char rbuf[8192];
    char hdr[65536];
    size_t hdr_len = 0;
    int in_header = 1;
    int status = 0;
    int is_listing = 0;

    for (;;) {
        ssize_t r = recv(s, rbuf, sizeof(rbuf), 0);
        if (r <= 0) break;

        if (in_header) {
            if (hdr_len + (size_t)r >= sizeof(hdr)) break;
            memcpy(hdr + hdr_len, rbuf, (size_t)r);
            hdr_len += (size_t)r;
            hdr[hdr_len] = '\0';

            char *sep = strstr(hdr, "\r\n\r\n");
            int off = 0;
            if (sep) {
                off = 4;
            } else {
                sep = strstr(hdr, "\n\n");
                if (sep) off = 2;
            }
            if (!sep) continue;

            if (sscanf(hdr, "HTTP/%*d.%*d %d", &status) != 1 ||
                status < 200 || status >= 300) {
                break;
            }

            if (strstr(hdr, "\nX-Dir-Listing: 1") ||
                strstr(hdr, "\r\nX-Dir-Listing: 1")) {
                is_listing = 1;
            }

            size_t body = hdr_len - (size_t)((sep - hdr) + off);
            if (is_listing) {
                if (body) fwrite(sep + off, 1, body, stdout);
            } else {
                fp = fopen(file, "wb");
                if (!fp) break;
                if (body) fwrite(sep + off, 1, body, fp);
            }
            in_header = 0;
        } else {
            if (is_listing) {
                fwrite(rbuf, 1, (size_t)r, stdout);
            } else if (fp) {
                fwrite(rbuf, 1, (size_t)r, fp);
            }
        }
    }

    if (fp) fclose(fp);
    close(s);

    if (status >= 200 && status < 300) {
        if (!is_listing) {
            printf("Salvo: %s (HTTP %d)\n", file, status);
        }
        return 0;
    }

    fprintf(stderr, "falha HTTP (%d)\n", status);
    return 1;
}