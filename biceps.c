#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "creme.h"

/* ═══════════════════════════════════════════════════════════════════════
 * Variables globales
 * ═══════════════════════════════════════════════════════════════════════ */

struct elt     *contacts       = NULL;
pthread_mutex_t contacts_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int    server_running = 0;

static int       udp_sock          = -1;
static pthread_t udp_thread;
static char      local_pseudo[LPSEUDO + 1] = {0};

/* ═══════════════════════════════════════════════════════════════════════
 * Liste chaînée
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * ajouteElt – insère (pseudo, adip) trié alphabétiquement.
 * Met à jour l'IP si le pseudo existe déjà.
 */
void ajouteElt(char *pseudo, char *adip)
{
    pthread_mutex_lock(&contacts_mutex);

    struct elt *cur = contacts;
    while (cur) {
        if (strncmp(cur->nom, pseudo, LPSEUDO) == 0) {
            strncpy(cur->adip, adip, 15);
            cur->adip[15] = '\0';
            pthread_mutex_unlock(&contacts_mutex);
            return;
        }
        cur = cur->next;
    }

    struct elt *e = malloc(sizeof(*e));
    if (!e) {
        pthread_mutex_unlock(&contacts_mutex);
        return;
    }
    strncpy(e->nom, pseudo, LPSEUDO);
    e->nom[LPSEUDO] = '\0';
    strncpy(e->adip, adip, 15);
    e->adip[15] = '\0';
    e->next = NULL;

    if (!contacts || strncmp(pseudo, contacts->nom, LPSEUDO) < 0) {
        e->next  = contacts;
        contacts = e;
    } else {
        struct elt *prev = contacts;
        while (prev->next &&
               strncmp(pseudo, prev->next->nom, LPSEUDO) >= 0)
            prev = prev->next;
        e->next    = prev->next;
        prev->next = e;
    }

    pthread_mutex_unlock(&contacts_mutex);
}

/*
 * supprimeElt – retire l'entrée dont l'IP est adip.
 */
void supprimeElt(char *adip)
{
    pthread_mutex_lock(&contacts_mutex);

    struct elt **pp = &contacts;
    while (*pp) {
        if (strncmp((*pp)->adip, adip, 15) == 0) {
            struct elt *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            break;
        }
        pp = &(*pp)->next;
    }

    pthread_mutex_unlock(&contacts_mutex);
}

/*
 * listeElts – affiche tous les contacts.
 */
void listeElts(void)
{
    pthread_mutex_lock(&contacts_mutex);

    struct elt *cur = contacts;
    if (!cur)
        printf("(aucun utilisateur connecté)\n");
    while (cur) {
        printf("%s : %s\n", cur->adip, cur->nom);
        cur = cur->next;
    }

    pthread_mutex_unlock(&contacts_mutex);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Réseau UDP
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * send_udp – envoie un datagramme BEUIP à dest_ip.
 * Format : octet1 + pseudo + '\n' + message
 */
static void send_udp(const char *dest_ip, char octet1,
                     const char *pseudo, const char *msg)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;

    int bcast = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(BEUIP_PORT);
    inet_pton(AF_INET, dest_ip, &dst.sin_addr);

    char buf[MAX_MSG];
    int n = snprintf(buf, sizeof(buf), "%c%s\n%s",
                     octet1,
                     pseudo ? pseudo : "",
                     msg    ? msg    : "");
    sendto(s, buf, n, 0, (struct sockaddr *)&dst, sizeof(dst));
    close(s);
}

/*
 * broadcast – annonce en broadcast (connexion ou déconnexion).
 */
static void broadcast(char octet1, const char *pseudo)
{
    send_udp(BCAST_ADDR, octet1, pseudo, "");
}

/* ═══════════════════════════════════════════════════════════════════════
 * Commandes internes (sans passage par le réseau)
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * commande – exécute list ('3'), message privé ('4'), broadcast ('5').
 */
void commande(char octet1, char *message, char *pseudo)
{
    if (octet1 == '3') {
        listeElts();
        return;
    }

    if (octet1 == '5') {
        pthread_mutex_lock(&contacts_mutex);
        struct elt *cur = contacts;
        while (cur) {
            send_udp(cur->adip, '5', "", message);
            cur = cur->next;
        }
        pthread_mutex_unlock(&contacts_mutex);
        printf("[broadcast] %s\n", message);
        return;
    }

    if (octet1 == '4') {
        pthread_mutex_lock(&contacts_mutex);
        struct elt *cur = contacts;
        while (cur) {
            if (strncmp(cur->nom, pseudo, LPSEUDO) == 0) {
                send_udp(cur->adip, '4', "", message);
                pthread_mutex_unlock(&contacts_mutex);
                printf("[-> %s] %s\n", pseudo, message);
                return;
            }
            cur = cur->next;
        }
        pthread_mutex_unlock(&contacts_mutex);
        printf("Utilisateur '%s' introuvable.\n", pseudo);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Thread serveur UDP
 * ═══════════════════════════════════════════════════════════════════════ */

void *serveur_udp(void *p)
{
    char *pseudo = (char *)p;

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket UDP");
        server_running = 0;
        return NULL;
    }

    int reuse = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(BEUIP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind UDP");
        close(udp_sock);
        udp_sock       = -1;
        server_running = 0;
        return NULL;
    }

    broadcast('1', pseudo);

    char buf[MAX_MSG];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    while (server_running) {
        ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&src, &src_len);
        if (n <= 0) break;
        buf[n] = '\0';

        char src_ip[16];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        char octet1 = buf[0];

        char sender[LPSEUDO + 1] = {0};
        char *nl = strchr(buf + 1, '\n');
        if (nl) {
            int len = (int)(nl - (buf + 1));
            if (len > LPSEUDO) len = LPSEUDO;
            strncpy(sender, buf + 1, len);
            sender[len] = '\0';
        }
        char *payload = nl ? nl + 1 : "";

        switch (octet1) {
        case '1':
            ajouteElt(sender, src_ip);
            send_udp(src_ip, '2', pseudo, "");
            printf("\n[+] %s (%s) connecté\n", sender, src_ip);
            break;
        case '2':
            ajouteElt(sender, src_ip);
            printf("\n[+] %s (%s) présent\n", sender, src_ip);
            break;
        case '0':
            supprimeElt(src_ip);
            printf("\n[-] %s (%s) déconnecté\n", sender, src_ip);
            break;
        case '9':
            /* Utilisé en interne pour débloquer recvfrom */
            break;
        case '4':
            printf("\n[msg de %s] %s\n", src_ip, payload);
            break;
        case '5':
            printf("\n[all de %s] %s\n", src_ip, payload);
            break;
        default:
            fprintf(stderr,
                    "[WARN] octet1='%c' non géré – possible piratage (%s)\n",
                    octet1, src_ip);
            break;
        }
    }

    close(udp_sock);
    udp_sock = -1;
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Gestion du serveur (start / stop)
 * ═══════════════════════════════════════════════════════════════════════ */

static void beuip_start(char *pseudo)
{
    if (server_running) {
        printf("Serveur déjà actif (pseudo : %s)\n", local_pseudo);
        return;
    }
    strncpy(local_pseudo, pseudo, LPSEUDO);
    local_pseudo[LPSEUDO] = '\0';
    server_running = 1;

    if (pthread_create(&udp_thread, NULL, serveur_udp, local_pseudo) != 0) {
        perror("pthread_create");
        server_running = 0;
    }
}

static void beuip_stop(void)
{
    if (!server_running) {
        printf("Serveur non actif.\n");
        return;
    }
    server_running = 0;
    broadcast('0', local_pseudo);
    send_udp("127.0.0.1", '9', local_pseudo, "");
    pthread_join(udp_thread, NULL);
    local_pseudo[0] = '\0';
    printf("Serveur arrêté.\n");
}

/* ═══════════════════════════════════════════════════════════════════════
 * Interpréteur de commandes
 * ═══════════════════════════════════════════════════════════════════════ */

static void beuip_message(char *args)
{
    if (!server_running) {
        printf("Serveur non actif. Utilisez 'beuip start <pseudo>'.\n");
        return;
    }

    char *dest = strtok(args, " \t");
    char *msg  = strtok(NULL, "");

    if (!dest || !msg) {
        printf("Usage: beuip message <pseudo|all> <message>\n");
        return;
    }
    while (*msg == ' ' || *msg == '\t') msg++;

    if (strcmp(dest, "all") == 0)
        commande('5', msg, NULL);
    else
        commande('4', msg, dest);
}

static void interprete(char *ligne)
{
    ligne[strcspn(ligne, "\n")] = '\0';

    if (strncmp(ligne, "beuip ", 6) != 0) {
        printf("Commande inconnue : %s\n", ligne);
        return;
    }

    char *cmd = ligne + 6;

    if (strncmp(cmd, "start ", 6) == 0) {
        beuip_start(cmd + 6);
    } else if (strcmp(cmd, "stop") == 0) {
        beuip_stop();
    } else if (strcmp(cmd, "list") == 0) {
        commande('3', NULL, NULL);
    } else if (strncmp(cmd, "message ", 8) == 0) {
        char args[MAX_MSG];
        strncpy(args, cmd + 8, sizeof(args) - 1);
        args[sizeof(args) - 1] = '\0';
        beuip_message(args);
    } else {
        printf("Commande beuip inconnue : %s\n", cmd);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(void)
{
    char ligne[MAX_MSG];

    printf("biceps v3 – tapez 'beuip start <pseudo>' pour commencer.\n");
    printf("CTRL+D pour quitter.\n");

    while (printf("> "), fflush(stdout),
           fgets(ligne, sizeof(ligne), stdin))
        interprete(ligne);

    if (server_running)
        beuip_stop();

    pthread_mutex_lock(&contacts_mutex);
    struct elt *cur = contacts;
    while (cur) {
        struct elt *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    contacts = NULL;
    pthread_mutex_unlock(&contacts_mutex);

    printf("\nAu revoir.\n");
    return 0;
}