#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "beuip.h"

#define LBUF 512

/* ------------------------------------------------------------------ */
/*  Liste chaînée des utilisateurs (section critique protégée)         */
/* ------------------------------------------------------------------ */

static struct elt    *liste       = NULL;
static pthread_mutex_t liste_mtx  = PTHREAD_MUTEX_INITIALIZER;

/* Ajoute ou met à jour un élément, maintient l'ordre alphabétique */
void ajoute_elt(const char *pseudo, const char *adip)
{
    pthread_mutex_lock(&liste_mtx);

    struct elt **cur = &liste;
    while (*cur) {
        int cmp = strcmp((*cur)->nom, pseudo);
        if (cmp == 0) {                         /* mise à jour IP */
            strncpy((*cur)->adip, adip, 15);
            (*cur)->adip[15] = '\0';
            pthread_mutex_unlock(&liste_mtx);
            return;
        }
        if (cmp > 0) break;                     /* insertion avant */
        cur = &(*cur)->next;
    }

    struct elt *n = malloc(sizeof(*n));
    if (!n) { pthread_mutex_unlock(&liste_mtx); return; }
    strncpy(n->nom,  pseudo, LPSEUDO);  n->nom[LPSEUDO]  = '\0';
    strncpy(n->adip, adip,   15);       n->adip[15]       = '\0';
    n->next = *cur;
    *cur    = n;

#ifdef TRACE
    printf("[TRACE] Nouvel utilisateur : %s (%s)\n", pseudo, adip);
#endif

    pthread_mutex_unlock(&liste_mtx);
}

/* Supprime l'élément correspondant à une adresse IP */
static void supprime_elt(const char *adip)
{
    pthread_mutex_lock(&liste_mtx);

    struct elt **cur = &liste;
    while (*cur) {
        if (strcmp((*cur)->adip, adip) == 0) {
            struct elt *tmp = *cur;
            *cur = tmp->next;
            free(tmp);
            pthread_mutex_unlock(&liste_mtx);
            return;
        }
        cur = &(*cur)->next;
    }

    pthread_mutex_unlock(&liste_mtx);
}

/* Affiche la liste (format demandé par le prof) */
void beuip_list(void)
{
    pthread_mutex_lock(&liste_mtx);

    struct elt *cur = liste;
    while (cur) {
        printf("%s : %s\n", cur->adip, cur->nom);
        cur = cur->next;
    }

    pthread_mutex_unlock(&liste_mtx);
}

/* Libère toute la liste */
static void libere_liste(void)
{
    pthread_mutex_lock(&liste_mtx);
    struct elt *cur = liste;
    while (cur) {
        struct elt *tmp = cur->next;
        free(cur);
        cur = tmp;
    }
    liste = NULL;
    pthread_mutex_unlock(&liste_mtx);
}

/* ------------------------------------------------------------------ */
/*  Envoi de messages                                                   */
/* ------------------------------------------------------------------ */

/* Cherche l'IP d'un pseudo dans la liste (retourne 0 si trouvé) */
static int trouve_ip(const char *pseudo, char *adip_out)
{
    pthread_mutex_lock(&liste_mtx);
    struct elt *cur = liste;
    while (cur) {
        if (strcmp(cur->nom, pseudo) == 0) {
            strncpy(adip_out, cur->adip, 15);
            adip_out[15] = '\0';
            pthread_mutex_unlock(&liste_mtx);
            return 0;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&liste_mtx);
    return -1;
}

static int make_socket_send(void)
{
    int sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) return -1;
    int opt = 1;
    setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    return sid;
}

static void send_msg(int sid, const char *ip, const char *msg, int len)
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(PORT);
    dst.sin_addr.s_addr = inet_addr(ip);
    sendto(sid, msg, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

/* Envoi d'un MP (octet1 = '9') directement à l'IP cible */
void beuip_message(const char *target, const char *text)
{
    char adip[16];
    if (trouve_ip(target, adip) != 0) {
        printf("Erreur : utilisateur '%s' introuvable.\n", target);
        return;
    }

    char buf[LBUF];
    int  len = snprintf(buf, LBUF, "9BEUIP%s", text);

    int sid = make_socket_send();
    if (sid < 0) return;
    send_msg(sid, adip, buf, len);
    close(sid);

#ifdef TRACE
    printf("[TRACE] MP envoye a %s (%s) : %s\n", target, adip, text);
#endif
}

/* Envoi d'un message à tous (octet1 = '9' broadcast) */
void beuip_message_all(const char *text)
{
    char buf[LBUF];
    int  len = snprintf(buf, LBUF, "9BEUIP%s", text);

    int sid = make_socket_send();
    if (sid < 0) return;

    pthread_mutex_lock(&liste_mtx);
    struct elt *cur = liste;
    while (cur) {
        send_msg(sid, cur->adip, buf, len);
        cur = cur->next;
    }
    pthread_mutex_unlock(&liste_mtx);

    close(sid);

#ifdef TRACE
    printf("[TRACE] Message envoye a tous : %s\n", text);
#endif
}

/* ------------------------------------------------------------------ */
/*  Thread serveur UDP                                                  */
/* ------------------------------------------------------------------ */

static int          server_fd  = -1;
static int          running    = 0;
static pthread_t    udp_thread;
static char         mon_pseudo[LPSEUDO + 1];

/* Envoie le broadcast d'identification ('1') */
static void send_id_broadcast(int sid)
{
    char msg[LBUF];
    int  len = snprintf(msg, LBUF, "1BEUIP%s", mon_pseudo);

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(PORT);
    dst.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);

    sendto(sid, msg, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

/* Répond à un '1' par un '2' */
static void send_ar(int sid, struct sockaddr_in *cli)
{
    char buf[LBUF];
    int  len = snprintf(buf, LBUF, "2BEUIP%s", mon_pseudo);
    sendto(sid, buf, len, 0, (struct sockaddr *)cli, sizeof(*cli));
}

/* Traite un message '0' : départ d'un utilisateur */
static void handle_depart(const char *adip)
{
    supprime_elt(adip);
#ifdef TRACE
    printf("[TRACE] Depart de %s\n", adip);
#endif
}

/* Boucle principale du serveur UDP */
static void *serveur_udp(void *arg)
{
    (void)arg;

    struct sockaddr_in cli;
    socklen_t          cls = sizeof(cli);
    char               buf[LBUF + 1];

    while (running) {
        int n = recvfrom(server_fd, buf, LBUF, 0,
                         (struct sockaddr *)&cli, &cls);
        if (n < 6) continue;
        if (strncmp(buf + 1, "BEUIP", 5) != 0) continue;

        buf[n] = '\0';
        char  code = buf[0];
        char *payload = buf + 6;
        char  adip[16];
        strncpy(adip, inet_ntoa(cli.sin_addr), 15);
        adip[15] = '\0';

#ifdef TRACE
        printf("[TRACE] Recu code='%c' de %s\n", code, adip);
#endif

        if (code == '0') {
            handle_depart(adip);
        } else if (code == '1') {
            ajoute_elt(payload, adip);
            send_ar(server_fd, &cli);
        } else if (code == '2') {
            ajoute_elt(payload, adip);
        } else if (code == '9') {
            printf("\n[Message de %s] : %s\n", adip, payload);
            fflush(stdout);
        } else {
            printf("[WARN] Code '%c' recu de %s - tentative de piratage ?\n",
                   code, adip);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Démarrage / Arrêt                                                   */
/* ------------------------------------------------------------------ */

int beuip_start(const char *pseudo)
{
    if (running) {
        printf("Serveur deja actif.\n");
        return -1;
    }

    strncpy(mon_pseudo, pseudo, LPSEUDO);
    mon_pseudo[LPSEUDO] = '\0';

    /* Création du socket UDP */
    server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,  &opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(PORT);
    srv.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind"); close(server_fd); server_fd = -1; return -1;
    }

    running = 1;
    if (pthread_create(&udp_thread, NULL, serveur_udp, NULL) != 0) {
        perror("pthread_create"); running = 0;
        close(server_fd); server_fd = -1; return -1;
    }

    /* Broadcast d'identification */
    send_id_broadcast(server_fd);

#ifdef TRACE
    printf("[TRACE] Serveur UDP demarre avec pseudo '%s'\n", pseudo);
#endif
    return 0;
}

int beuip_stop(void)
{
    if (!running) {
        printf("Serveur non actif.\n");
        return -1;
    }

    /* Envoi d'un '0' a tout le monde */
    char buf[LBUF];
    int  len = snprintf(buf, LBUF, "0BEUIP%s", mon_pseudo);
    int  sid = make_socket_send();
    if (sid >= 0) {
        pthread_mutex_lock(&liste_mtx);
        struct elt *cur = liste;
        while (cur) {
            send_msg(sid, cur->adip, buf, len);
            cur = cur->next;
        }
        pthread_mutex_unlock(&liste_mtx);
        close(sid);
    }

    running = 0;
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
    server_fd = -1;
    pthread_join(udp_thread, NULL);

    libere_liste();

#ifdef TRACE
    printf("[TRACE] Serveur UDP arrete.\n");
#endif
    return 0;
}
