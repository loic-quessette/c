#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>


#define SERVER_ADDR "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

int client_fd;
char username[50];

void handle_sigint(int sig) {
    printf("\n\033[1;31mDéconnexion forcée par l'utilisateur (Ctrl+C).\033[0m\n");
    if (client_fd >= 0) {
        close(client_fd); // Fermer proprement le socket
    }
    exit(0); // Quitter le programme
}

void* receive_messages(void* arg) {
    char buffer[BUFFER_SIZE];
    while (1) {
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("\n\033[1;31m[Vous vous êtes déconnecté.]\033[0m On espère vous revoir très vite !\n");
            break;
        }
        buffer[bytes_read] = '\0';

        // Amélioration de l'affichage des messages reçus
        printf("\033[1;34m%s\033[0m\n", buffer); // Messages en bleu
    }
    return NULL;
}

int main() {
    // Associer le gestionnaire de signal
    signal(SIGINT, handle_sigint);

    struct sockaddr_in server_addr;
    pthread_t receive_thread;

    // Création du socket
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("Erreur de création du socket");
        exit(1);
    }

    // Configuration de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    // Connexion au serveur
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erreur de connexion au serveur");
        close(client_fd);
        exit(1);
    }

    // Saisie et validation du nom d'utilisateur
    printf("\033[1;32m=== Bienvenue dans le Chat ===\033[0m\n");
    printf("Entrez votre \033[1;33mnom d'utilisateur\033[0m : ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    // Création du thread pour recevoir les messages
    if (pthread_create(&receive_thread, NULL, receive_messages, NULL) != 0) {
        perror("Erreur de création du thread pour recevoir les messages");
        close(client_fd);
        exit(1);
    }

    // Saisie et validation du canal
    char channel_name[50];
    printf("\033[1;36mEntrez le nom du canal\033[0m : ");
    fgets(channel_name, sizeof(channel_name), stdin);
    channel_name[strcspn(channel_name, "\n")] = 0;

    // Envoi du nom d'utilisateur et du canal au serveur
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s:%s", username, channel_name);
    write(client_fd, buffer, strlen(buffer));


    char message[BUFFER_SIZE];
    printf("\n\033[1;32m=== Chat ===\033[0m\n");
    printf("\033[1;35mUtilisez /exit pour quitter et /join [nom du canal] pour changer de canal.\033[0m\n");

	// Boucle principale pour l'envoi des messages
    while (1) {
        printf("\033[1;33mVous :\033[0m ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0;

        if (strcmp(message, "/exit") == 0) {
            printf("\033[1;31mDéconnexion...\033[0m\n");
            break;
        }

        // Commande pour changer de canal
        if (strncmp(message, "/join ", 6) == 0) {
            write(client_fd, message, strlen(message));
            printf("\033[1;36mChangement de canal demandé...\033[0m\n");

            // Ajout d'une pause pour montrer la transition
            sleep(1);

            continue;
        }

        if (strlen(message) > 0) {
            write(client_fd, message, strlen(message));
        }
    }

    close(client_fd);
    return 0;
}