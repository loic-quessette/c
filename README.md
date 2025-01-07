# c
# Lancer le serveur :

gcc -o server server.c -lpthread (pas besoin de port il est défini dans le code)

./server

# Lancer le client :

gcc -o client client.c

./client

# Fonctionnalité en plus

J'ai rajouté des couleurs du coté client pour rendre le chat plus lisible et plus agréable.

J'ai également ajouté l'heure à laquelle les messages sont envoyés ainsi que des logs côté serveur pour voir les 
connexions des clients.