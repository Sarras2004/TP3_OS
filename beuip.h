#ifndef BEUIP_H
#define BEUIP_H

#define PORT         9998
#define LPSEUDO      23
#define BROADCAST_ADDR "192.168.88.255"

/* Element de la liste chaînée des utilisateurs */
struct elt {
    char        nom[LPSEUDO + 1];
    char        adip[16];
    struct elt *next;
};

/* Fonctions publiques du module beuip */
int  beuip_start(const char *pseudo);
int  beuip_stop(void);
void beuip_list(void);
void beuip_message(const char *target, const char *text);
void beuip_message_all(const char *text);

#endif /* BEUIP_H */
