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

typedef struct Message {
    char text[1024];
    char sender[50];
    time_t timestamp;
} Message;

typedef struct Channel {
    char name[50];
    Message* messages;
    int message_count;
    int message_capacity;
    int* clients;
    int client_count;
    int client_capacity;
} Channel;

Channel* channels = NULL;
int channel_count = 0;
int channel_capacity = 0;
int server_fd;

// permet de fermer la connexion en cas de ctrl+c
void handle_sigint(int sig) {
    printf("\nFermeture du serveur...\n");
    close(server_fd);
    exit(0);
}

// permet d'envoyer l'historique du canal
void send_channel_history(int client_fd, int channel_id) {
    if (channel_id < 0 || channel_id >= channel_count) {
        return;
    }
    Channel* channel = &channels[channel_id];
    char message[BUFFER_SIZE];
    char time_str[100];

    // si le canal ne contient aucun message
    if (channel->message_count == 0) {
        snprintf(message, sizeof(message), "Bienvenue dans le canal %s. Aucun message encore.\n", channel->name);
        write(client_fd, message, strlen(message));
    }
    else {
        snprintf(message, sizeof(message), "Bienvenue dans le canal %s. Voici les messages précédents :\n", channel->name);
        write(client_fd, message, strlen(message));
    }

    //
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

// permet de créer ou de rejoindre un canal
int create_or_join_channel(char* channel_name, char* username) {
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, channel_name) == 0) {
            printf("L'utilisateur %s a rejoint le canal: %s\n", username, channel_name);
            return i;
        }
    }

    // vérifie si la capacité actuelle du tableau channels est suffisante pour ajouter un nouveau canal
    if (channel_count == channel_capacity) {
        channel_capacity = channel_capacity == 0 ? 1 : channel_capacity * 2;
        channels = realloc(channels, channel_capacity * sizeof(Channel));
    }

    // permet d'allouer dynamiquement de la mémoire pour le nouveau canal
    Channel* new_channel = &channels[channel_count++];
    strncpy(new_channel->name, channel_name, sizeof(new_channel->name) - 1);
    new_channel->message_count = 0;
    new_channel->message_capacity = 10;
    new_channel->messages = malloc(new_channel->message_capacity * sizeof(Message));
    new_channel->client_count = 0;
    new_channel->client_capacity = 10;
    new_channel->clients = malloc(new_channel->client_capacity * sizeof(int));

    printf("L'utilisateur %s a créé le canal: %s\n", username, channel_name);
    return channel_count - 1;
}

void broadcast_message(int channel_id, const char* message, const char* sender) {
    if (channel_id < 0 || channel_id >= channel_count) {
        return;
    }
    Channel* channel = &channels[channel_id];

    // si le nombre de messages dépasse la capacité actuelle, on double la capacité
    if (channel->message_count == channel->message_capacity) {
        channel->message_capacity *= 2;
        channel->messages = realloc(channel->messages, channel->message_capacity * sizeof(Message));
    }

    Message* new_message = &channel->messages[channel->message_count++];
    strncpy(new_message->text, message, sizeof(new_message->text) - 1);
    strncpy(new_message->sender, sender, sizeof(new_message->sender) - 1);
    new_message->timestamp = time(NULL);

    // permet d'envoyer le message à tous les clients du canal
    for (int i = 0; i < channel->client_count; i++) {
        int client_fd = channel->clients[i];
        char broadcast_message[BUFFER_SIZE];

        char time_str[100];
        struct tm* time_info = localtime(&new_message->timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

        int max_msg_len = sizeof(broadcast_message) - 1;
        int sender_len = strlen(sender);
        int text_len = strlen(new_message->text);
        int time_str_len = strlen(time_str);

        int available_len = max_msg_len - (time_str_len + sender_len + 10);

        // si le message est trop long, on le tronque
        if (text_len > available_len) {
            text_len = available_len;
        }

        int written = snprintf(broadcast_message, sizeof(broadcast_message),
            "[%s] %s: %.*s\n", time_str, sender, text_len, new_message->text);

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

    // récupère l'adresse IP
    getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
    char* client_ip = inet_ntoa(client_addr.sin_addr);

    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    sscanf(buffer, "%49[^:]:%49s", username, channel_name);

    printf("Nouvelle connexion: IP=%s, Username=%s\n", client_ip, username);

    // permet de créer ou de rejoindre le canal donnée par le client
    channel_id = create_or_join_channel(channel_name, username);
    if (channel_id == -1) {
        write(client_fd, "Canal plein, veuillez réessayer plus tard.\n", 42);
        close(client_fd);
        return NULL;
    }

    Channel* channel = &channels[channel_id];
    // si le nombre de client est égal à la capacité du canal, on la double
    if (channel->client_count == channel->client_capacity) {
        channel->client_capacity *= 2;
        channel->clients = realloc(channel->clients, channel->client_capacity * sizeof(int));
    }
    channel->clients[channel->client_count++] = client_fd;

    send_channel_history(client_fd, channel_id);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }

        // gestion de la commande /join (permet au client de changer de canal)
        if (strncmp(buffer, "/join ", 6) == 0) {
            char new_channel_name[50];
            sscanf(buffer + 6, "%49s", new_channel_name);
            printf("%s est passé du canal %s au canal %s\n", username, channel->name, new_channel_name);

            for (int i = 0; i < channel->client_count; i++) {
                if (channel->clients[i] == client_fd) {
                    channel->clients[i] = channel->clients[channel->client_count - 1];
                    channel->client_count--;
                    break;
                }
            }

            int new_channel_id = create_or_join_channel(new_channel_name, username);
            if (new_channel_id == -1) {
                write(client_fd, "Impossible de rejoindre ce canal.\n", 34);
                continue;
            }

            channel_id = new_channel_id;
            channel = &channels[channel_id];
            if (channel->client_count == channel->client_capacity) {
                channel->client_capacity *= 2;
                channel->clients = realloc(channel->clients, channel->client_capacity * sizeof(int));
            }
            channel->clients[channel->client_count++] = client_fd;

            send_channel_history(client_fd, channel_id);

            snprintf(buffer, sizeof(buffer), "Vous avez rejoint le canal %s.\n", new_channel_name);
            write(client_fd, buffer, strlen(buffer));
            continue;
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
  // initialisation des variables
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t client_thread;

    signal(SIGINT, handle_sigint);

    // création du socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Erreur de création du socket serveur");
        exit(1);
    }

    // configuration de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // liaison du socket à l'adresse du serveur
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erreur de liaison du socket");
        close(server_fd);
        exit(1);
    }

    // écoute des connexions entrantes
    listen(server_fd, 5);
    printf("Serveur en écoute sur le port %d...\n", SERVER_PORT);

    // gestion des connexions entrantes
    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Erreur d'acceptation de connexion");
            continue;
        }

        pthread_create(&client_thread, NULL, handle_client, (void*)&client_fd);
    }

    // fermeture du socket
    close(server_fd);
    return 0;
}