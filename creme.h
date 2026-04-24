#ifndef CREME_H
#define CREME_H

#include <pthread.h>

/* ── Réseau ── */
#define BEUIP_PORT  9998
#define BCAST_ADDR  "192.168.88.255"
#define MAX_MSG     512
#define LPSEUDO     23

/* ── Liste chaînée des contacts ── */
struct elt {
    char        nom[LPSEUDO + 1];
    char        adip[16];
    struct elt *next;
};

/* ── Variables globales partagées ── */
extern struct elt      *contacts;
extern pthread_mutex_t  contacts_mutex;
extern volatile int     server_running;

#endif /* CREME_H */