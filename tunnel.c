#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#define BUFFER_SIZE 4096

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void forward_data(int src_fd, int dst_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;

    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(dst_fd, buffer, bytes_read);
        if (bytes_written < 0) {
            error("Error writing to socket");
        }
    }

    if (bytes_read < 0) {
        error("Error reading from socket");
    }
}

void handle_connection(int client_fd, const char *remote_addr, const char *remote_port) {
    int remote_fd;
    struct addrinfo hints, *res;
    int status;

    // Setup the remote connection
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(remote_addr, remote_port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        close(client_fd);
        return;
    }

    remote_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (remote_fd < 0) {
        perror("Error creating socket to remote");
        close(client_fd);
        freeaddrinfo(res);
        return;
    }

    if (connect(remote_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("Error connecting to remote");
        close(client_fd);
        close(remote_fd);
        freeaddrinfo(res);
        return;
    }

    freeaddrinfo(res);

    // Create child process to handle data forwarding
    pid_t pid = fork();
    if (pid < 0) {
        error("Error creating process");
    }

    if (pid == 0) {
        // Child process: forward data from remote to client
        forward_data(remote_fd, client_fd);
        close(client_fd);
        close(remote_fd);
        exit(0);
    } else {
        // Parent process: forward data from client to remote
        forward_data(client_fd, remote_fd);
        close(client_fd);
        close(remote_fd);
        wait(NULL); // Wait for the child process to finish
    }
}

int main(int argc, char *argv[]) {
    char *listen_addr = NULL;
    char *listen_port = NULL;
    char *remote_addr = NULL;
    char *remote_port = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_addr = strtok(argv[++i], ":");
            listen_port = strtok(NULL, ":");
        } else if (strcmp(argv[i], "--remote") == 0 && i + 1 < argc) {
            remote_addr = strtok(argv[++i], ":");
            remote_port = strtok(NULL, ":");
        }
    }

    if (!listen_addr || !listen_port || !remote_addr || !remote_port) {
        fprintf(stderr, "Usage: %s --listen <listening_address>:<listening_port> --remote <remote_address>:<remote_port>\n", argv[0]);
        exit(1);
    }

    int listen_fd, client_fd;
    struct addrinfo hints, *res;
    int status;

    // Setup the listening socket
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(listen_addr, listen_port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }

    listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd < 0) {
        error("Error creating listening socket");
    }

    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) < 0) {
        error("Error binding to listening address");
    }

    freeaddrinfo(res);

    if (listen(listen_fd, 10) < 0) {
        error("Error listening on socket");
    }

    printf("Listening on %s:%s, forwarding to %s:%s\n", listen_addr, listen_port, remote_addr, remote_port);

    // Main loop to accept and handle connections
    while (1) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            error("Error accepting connection");
        }

        pid_t pid = fork();
        if (pid < 0) {
            error("Error forking process");
        }

        if (pid == 0) {
            // Child process: handle the connection
            close(listen_fd);
            handle_connection(client_fd, remote_addr, remote_port);
            exit(0);
        } else {
            // Parent process: close the client socket and continue accepting
            close(client_fd);
        }
    }

    close(listen_fd);
    return 0;
}
