#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>   // Pour inet_addr
#include <pthread.h>     // Pour pthread_create
#include <time.h>        // Pour strftime

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_CHANNELS 10
#define MAX_MESSAGES 100
#define MAX_CLIENTS_PER_CHANNEL 10

// Structure d'un message
typedef struct Message {
    char text[1024];
    char sender[50];
    time_t timestamp;
} Message;

// Structure d'un canal
typedef struct Channel {
    char name[50];
    Message messages[MAX_MESSAGES];
    int message_count;
    int clients[MAX_CLIENTS_PER_CHANNEL]; // Liste des clients connectés à ce canal
    int client_count;
} Channel;

// Liste des canaux
Channel channels[MAX_CHANNELS];

// Fonction pour envoyer l'historique des messages d'un canal
void send_channel_history(int client_fd, int channel_id) {
    if (channel_id < 0 || channel_id >= MAX_CHANNELS) {
        return;
    }
    Channel* channel = &channels[channel_id];
    char message[BUFFER_SIZE];
    char time_str[100]; // Tampon pour stocker la date et l'heure formatées

    // Parcourir tous les messages du canal et les envoyer au client
    for (int i = 0; i < channel->message_count; i++) {
        // Formatage de la date et de l'heure avec strftime
        struct tm* time_info = localtime(&channel->messages[i].timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

        // Formater le message avec un tampon suffisamment grand
        int written = snprintf(message, sizeof(message), "[%s] %s: %s\n", time_str,
                               channel->messages[i].sender, channel->messages[i].text);

        // Vérifier si l'écriture a dépassé la capacité du tampon
        if (written >= sizeof(message)) {
            fprintf(stderr, "Avertissement : message tronqué.\n");
            message[sizeof(message) - 1] = '\0'; // Assurer la terminaison de la chaîne
        }

        // Envoyer le message au client
        write(client_fd, message, strlen(message));
    }
}

// Fonction pour créer ou rejoindre un canal
int create_or_join_channel(char* channel_name) {
    // Chercher si le canal existe déjà
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].message_count > 0 && strcmp(channels[i].name, channel_name) == 0) {
            return i; // Le canal existe, on le rejoint
        }
    }

    // Si le canal n'existe pas, créer un nouveau canal
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].message_count == 0) {
            strncpy(channels[i].name, channel_name, sizeof(channels[i].name) - 1);
            channels[i].message_count = 0;
            channels[i].client_count = 0; // Initialiser la liste des clients
            return i; // Nouveau canal créé
        }
    }

    // Aucun espace pour créer un nouveau canal
    return -1;
}

// Fonction pour envoyer un message à tous les clients d'un canal
void broadcast_message(int channel_id, const char* message, const char* sender) {
    if (channel_id < 0 || channel_id >= MAX_CHANNELS) {
        return;
    }
    Channel* channel = &channels[channel_id];

    // Créer un nouveau message à diffuser
    Message new_message;
    strncpy(new_message.text, message, sizeof(new_message.text) - 1);
    strncpy(new_message.sender, sender, sizeof(new_message.sender) - 1);
    new_message.timestamp = time(NULL);

    // Ajouter le message dans l'historique du canal
    if (channel->message_count < MAX_MESSAGES) {
        channel->messages[channel->message_count++] = new_message;
    }

    // Diffuser le message à tous les clients du canal, y compris celui qui envoie
    for (int i = 0; i < channel->client_count; i++) {
        int client_fd = channel->clients[i];
        char broadcast_message[BUFFER_SIZE];

        // Formatage sécurisé du message
        char time_str[100];
        struct tm* time_info = localtime(&new_message.timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

        // Calculer la taille maximale du message à envoyer
        int max_msg_len = sizeof(broadcast_message) - 1; // Laisser un octet pour le caractère null
        int sender_len = strlen(sender);
        int text_len = strlen(new_message.text);
        int time_str_len = strlen(time_str);

        // Limiter les longueurs pour ne pas dépasser la taille du buffer
        int available_len = max_msg_len - (time_str_len + sender_len + 10); // 10 est pour les autres caractères comme "[ ]", ": ", et "\n"

        if (text_len > available_len) {
            text_len = available_len; // Limiter la longueur du message
        }

        // Formater le message en utilisant snprintf
        int written = snprintf(broadcast_message, sizeof(broadcast_message),
            "[%s] %s: %.*s\n", time_str, sender, text_len, new_message.text);

        // Vérifier si l'écriture a dépassé la taille du tampon
        if (written >= sizeof(broadcast_message)) {
            fprintf(stderr, "Avertissement : message tronqué.\n");
            broadcast_message[sizeof(broadcast_message) - 1] = '\0'; // Assurer la terminaison de la chaîne
        }

        // Envoyer le message formaté au client
        write(client_fd, broadcast_message, strlen(broadcast_message));
    }
}

// Fonction pour gérer la communication avec un client
void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    char channel_name[50];
    char username[50];
    int channel_id = -1;

    // Recevoir le nom d'utilisateur et le nom du canal
    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    // Extraire le nom d'utilisateur et le nom du canal
    sscanf(buffer, "%49[^:]:%49s", username, channel_name);

    // Créer ou rejoindre le canal
    channel_id = create_or_join_channel(channel_name);
    if (channel_id == -1) {
        write(client_fd, "Canal plein, veuillez réessayer plus tard.\n", 42);
        close(client_fd);
        return NULL;
    }

    // Ajouter le client à la liste des clients du canal
    Channel* channel = &channels[channel_id];
    if (channel->client_count < MAX_CLIENTS_PER_CHANNEL) {
        channel->clients[channel->client_count++] = client_fd;
    } else {
        write(client_fd, "Le canal est plein.\n", 20);
        close(client_fd);
        return NULL;
    }

    // Envoyer l'historique des messages du canal
    send_channel_history(client_fd, channel_id);

    // Recevoir et envoyer des messages
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;  // Si le client se déconnecte
        }

        // Diffuser le message à tous les autres clients du canal
        broadcast_message(channel_id, buffer, username);
    }

    // Retirer le client de la liste des clients du canal
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

    // Créer le socket du serveur
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Erreur de création du socket serveur");
        exit(1);
    }

    // Configurer l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Lier le socket à l'adresse et au port
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erreur de liaison du socket");
        close(server_fd);
        exit(1);
    }

    // Mettre le serveur en écoute
    listen(server_fd, 5);
    printf("Serveur en écoute sur le port %d...\n", SERVER_PORT);

    // Accepter les connexions des clients
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Erreur d'acceptation de connexion");
            continue;
        }

        // Créer un thread pour gérer le client
        pthread_create(&client_thread, NULL, handle_client, (void*)&client_fd);
    }

    close(server_fd);
    return 0;
}