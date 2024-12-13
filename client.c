#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_ADDR "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

int client_fd;
char username[50];

void* receive_messages(void* arg) {
    char buffer[BUFFER_SIZE];
    while (1) {
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("Error reading or server disconnected.\n");
            break;
        }
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    return NULL;
}

int main() {
    struct sockaddr_in server_addr;
    pthread_t receive_thread;

    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("Socket creation error");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Server connection error");
        close(client_fd);
        exit(1);
    }

    printf("Enter your username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    if (pthread_create(&receive_thread, NULL, receive_messages, NULL) != 0) {
        perror("Receive thread creation error");
        close(client_fd);
        exit(1);
    }

    char channel_name[50];
    printf("Enter channel name: ");
    fgets(channel_name, sizeof(channel_name), stdin);
    channel_name[strcspn(channel_name, "\n")] = 0;

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s:%s", username, channel_name);
    write(client_fd, buffer, strlen(buffer));

    char message[BUFFER_SIZE];
    while (1) {
        printf("You: ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0;

        if (strlen(message) > 0) {
            write(client_fd, message, strlen(message));
        }
    }

    close(client_fd);
    return 0;
}