#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>   // Pour inet_addr
#include <pthread.h>     // Pour pthread_create

#define SERVER_ADDR "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

int client_fd;
char username[50];  // Nom d'utilisateur

void* receive_messages(void* arg) {
    char buffer[BUFFER_SIZE];
    while (1) {
        // Recevoir les messages du serveur
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("Erreur de lecture ou serveur déconnecté.\n");
            break;
        }
        buffer[bytes_read] = '\0';  // Assurer la terminaison de la chaîne
        printf("%s", buffer); // Afficher les messages reçus
    }
    return NULL;
}

int main() {
    struct sockaddr_in server_addr;
    pthread_t receive_thread;

    // Créer un socket pour le client
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("Erreur de création de socket");
        exit(1);
    }

    // Configurer l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    // Connecter le client au serveur
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erreur de connexion au serveur");
        close(client_fd);
        exit(1);
    }

    // Demander au client de saisir un nom d'utilisateur
    printf("Entrez votre nom d'utilisateur: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0; // Supprimer le saut de ligne

    // Créer un thread pour recevoir les messages du serveur
    if (pthread_create(&receive_thread, NULL, receive_messages, NULL) != 0) {
        perror("Erreur de création du thread de réception");
        close(client_fd);
        exit(1);
    }

    // Demander au client de saisir le nom du canal
    char channel_name[50];
    printf("Entrez le nom du canal: ");
    fgets(channel_name, sizeof(channel_name), stdin);
    channel_name[strcspn(channel_name, "\n")] = 0; // Supprimer le saut de ligne

    // Envoyer le nom du canal et le nom d'utilisateur au serveur
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s:%s", username, channel_name);
    write(client_fd, buffer, strlen(buffer));

    // Recevoir et envoyer des messages
    char message[BUFFER_SIZE];
    while (1) {
        printf("Vous: ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0; // Supprimer le saut de ligne

        if (strlen(message) > 0) {
            // Envoyer le message au serveur
            write(client_fd, message, strlen(message));
        }
    }

    close(client_fd);
    return 0;
}