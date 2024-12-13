#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_CHANNELS 10
#define MAX_MESSAGES 100
#define MAX_CLIENTS_PER_CHANNEL 10

typedef struct Message {
    char text[1024];
    char sender[50];
    time_t timestamp;
} Message;

typedef struct Channel {
    char name[50];
    Message messages[MAX_MESSAGES];
    int message_count;
    int clients[MAX_CLIENTS_PER_CHANNEL];
    int client_count;
} Channel;

Channel channels[MAX_CHANNELS];

void send_channel_history(int client_fd, int channel_id) {
    if (channel_id < 0 || channel_id >= MAX_CHANNELS) {
        return;
    }
    Channel* channel = &channels[channel_id];
    char message[BUFFER_SIZE];
    char time_str[100];

    if (channel->message_count == 0) {
        snprintf(message, sizeof(message), "Bienvenue dans le canal %s. Aucun message encore.\n", channel->name);
        write(client_fd, message, strlen(message));
    }

    for (int i = 0; i < channel->message_count; i++) {
        struct tm* time_info = localtime(&channel->messages[i].timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

        int written = snprintf(message, sizeof(message), "[%s] %s: %s\n", time_str,
                               channel->messages[i].sender, channel->messages[i].text);

        if (written >= sizeof(message)) {
            fprintf(stderr, "Avertissement : message tronqué.\n");
            message[sizeof(message) - 1] = '\0';
        }

        write(client_fd, message, strlen(message));
    }
}

int create_or_join_channel(char* channel_name) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (strcmp(channels[i].name, channel_name) == 0) {
            printf("User joined channel: %s\n", channel_name);
            return i;
        }
    }

    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].client_count == 0) {
            strncpy(channels[i].name, channel_name, sizeof(channels[i].name) - 1);
            channels[i].message_count = 0;
            channels[i].client_count = 0;

            printf("Channel created: %s\n", channel_name);
            return i;
        }
    }

    return -1;
}

void broadcast_message(int channel_id, const char* message, const char* sender) {
    if (channel_id < 0 || channel_id >= MAX_CHANNELS) {
        return;
    }
    Channel* channel = &channels[channel_id];

    Message new_message;
    strncpy(new_message.text, message, sizeof(new_message.text) - 1);
    strncpy(new_message.sender, sender, sizeof(new_message.sender) - 1);
    new_message.timestamp = time(NULL);

    if (channel->message_count < MAX_MESSAGES) {
        channel->messages[channel->message_count++] = new_message;
    }

    for (int i = 0; i < channel->client_count; i++) {
        int client_fd = channel->clients[i];
        char broadcast_message[BUFFER_SIZE];

        char time_str[100];
        struct tm* time_info = localtime(&new_message.timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

        int max_msg_len = sizeof(broadcast_message) - 1;
        int sender_len = strlen(sender);
        int text_len = strlen(new_message.text);
        int time_str_len = strlen(time_str);

        int available_len = max_msg_len - (time_str_len + sender_len + 10);

        if (text_len > available_len) {
            text_len = available_len;
        }

        int written = snprintf(broadcast_message, sizeof(broadcast_message),
            "[%s] %s: %.*s\n", time_str, sender, text_len, new_message.text);

        if (written >= sizeof(broadcast_message)) {
            fprintf(stderr, "Avertissement : message tronqué.\n");
            broadcast_message[sizeof(broadcast_message) - 1] = '\0';
        }

        write(client_fd, broadcast_message, strlen(broadcast_message));
    }
}

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    char channel_name[50];
    char username[50];
    int channel_id = -1;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
    char* client_ip = inet_ntoa(client_addr.sin_addr);

    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    sscanf(buffer, "%49[^:]:%49s", username, channel_name);

    printf("Client connected: IP=%s, Username=%s\n", client_ip, username);

    channel_id = create_or_join_channel(channel_name);
    if (channel_id == -1) {
        write(client_fd, "Canal plein, veuillez réessayer plus tard.\n", 42);
        close(client_fd);
        return NULL;
    }

    Channel* channel = &channels[channel_id];
    if (channel->client_count < MAX_CLIENTS_PER_CHANNEL) {
        channel->clients[channel->client_count++] = client_fd;
    } else {
        write(client_fd, "Le canal est plein.\n", 20);
        close(client_fd);
        return NULL;
    }

    if (channel->message_count == 0) {
        char welcome_msg[BUFFER_SIZE];
        snprintf(welcome_msg, sizeof(welcome_msg), "Bienvenue dans le canal %s ! Vous êtes le premier à y entrer.\n", channel_name);
        write(client_fd, welcome_msg, strlen(welcome_msg));
    }

    send_channel_history(client_fd, channel_id);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }

        broadcast_message(channel_id, buffer, username);
    }

    for (int i = 0; i < channel->client_count; i++) {
        if (channel->clients[i] == client_fd) {
            channel->clients[i] = channel->clients[channel->client_count - 1];
            channel->client_count--;
            break;
        }
    }

    close(client_fd);
    return NULL;
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t client_thread;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Erreur de création du socket serveur");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erreur de liaison du socket");
        close(server_fd);
        exit(1);
    }

    listen(server_fd, 5);
    printf("Serveur en écoute sur le port %d...\n", SERVER_PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Erreur d'acceptation de connexion");
            continue;
        }

        pthread_create(&client_thread, NULL, handle_client, (void*)&client_fd);
    }

    close(server_fd);
    return 0;
}