#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "beuip.h"

#define MAXPAR  10
#define NBMAXC  20

/* ------------------------------------------------------------------ */
/*  Structures et variables globales                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char *nom;
    int (*fonction)(int, char **);
} ComInt;

static ComInt  tab_com_int[NBMAXC];
static int     nb_com_int = 0;
static char   *mots[MAXPAR];

/* ------------------------------------------------------------------ */
/*  Gestion des signaux                                                 */
/* ------------------------------------------------------------------ */

static void handle_sigint(int sig)
{
    (void)sig;
    printf("\n[biceps] Interruption ignoree. Utilisez 'exit' pour quitter.\n");
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}

/* ------------------------------------------------------------------ */
/*  Commandes internes classiques                                       */
/* ------------------------------------------------------------------ */

static int cmd_sortie(int n, char *p[])
{
    (void)n; (void)p;
    printf("Au revoir !\n");
    exit(0);
}

static int cmd_cd(int n, char *p[])
{
    if (n < 2) {
        fprintf(stderr, "cd: argument manquant\n");
    } else if (chdir(p[1]) != 0) {
        perror("cd");
    }
    return 1;
}

static int cmd_pwd(int n, char *p[])
{
    (void)n; (void)p;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s\n", cwd);
    else
        perror("pwd");
    return 1;
}

static int cmd_vers(int n, char *p[])
{
    (void)n; (void)p;
    printf("biceps version 3.00 - Polytech Sorbonne 2026\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Commandes internes beuip                                            */
/* ------------------------------------------------------------------ */

static int cmd_beuip(int n, char *p[])
{
    if (n < 2) {
        fprintf(stderr, "beuip: sous-commande manquante\n");
        return 1;
    }

    if (strcmp(p[1], "start") == 0) {
        if (n < 3) {
            fprintf(stderr, "beuip start: pseudo manquant\n");
            return 1;
        }
        beuip_start(p[2]);

    } else if (strcmp(p[1], "stop") == 0) {
        beuip_stop();

    } else if (strcmp(p[1], "list") == 0) {
        beuip_list();

    } else if (strcmp(p[1], "message") == 0) {
        if (n < 4) {
            fprintf(stderr, "beuip message: arguments manquants\n");
            return 1;
        }

        char msg[512] = "";
        for (int i = 3; i < n; i++) {
            if (i > 3) strncat(msg, " ", sizeof(msg) - strlen(msg) - 1);
            strncat(msg, p[i], sizeof(msg) - strlen(msg) - 1);
        }

        if (strcmp(p[2], "all") == 0)
            beuip_message_all(msg);
        else
            beuip_message(p[2], msg);

    } else {
        fprintf(stderr, "beuip: sous-commande inconnue '%s'\n", p[1]);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Gestion du tableau de commandes internes                            */
/* ------------------------------------------------------------------ */

static void ajoute_com(char *nom, int (*f)(int, char **))
{
    if (nb_com_int >= NBMAXC) {
        fprintf(stderr, "Erreur : NBMAXC trop petit !\n");
        exit(1);
    }
    tab_com_int[nb_com_int].nom      = nom;
    tab_com_int[nb_com_int].fonction = f;
    nb_com_int++;
}

static void maj_com_int(void)
{
    ajoute_com("exit",  cmd_sortie);
    ajoute_com("cd",    cmd_cd);
    ajoute_com("pwd",   cmd_pwd);
    ajoute_com("vers",  cmd_vers);
    ajoute_com("beuip", cmd_beuip);
}

static int exec_com_int(int n, char **p)
{
    for (int i = 0; i < nb_com_int; i++) {
        if (strcmp(p[0], tab_com_int[i].nom) == 0) {
            tab_com_int[i].fonction(n, p);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Analyse de la ligne de commande                                     */
/* ------------------------------------------------------------------ */

static int analyse_com(char *b)
{
    int   n = 0;
    char *token;
    while ((token = strsep(&b, " \t\n")) != NULL) {
        if (*token != '\0' && n < MAXPAR - 1)
            mots[n++] = strdup(token);
    }
    mots[n] = NULL;
    return n;
}

/* ------------------------------------------------------------------ */
/*  Exécution des commandes externes                                    */
/* ------------------------------------------------------------------ */

static void exec_com_ext(char **p)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (execvp(p[0], p) == -1) {
            fprintf(stderr, "biceps: commande introuvable: %s\n", p[0]);
            exit(1);
        }
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork");
    }
}

/* ------------------------------------------------------------------ */
/*  Programme principal                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    signal(SIGINT, handle_sigint);
    read_history(".biceps_history");
    maj_com_int();

    char  hostname[256];
    gethostname(hostname, sizeof(hostname));
    char *user = getenv("USER");
    if (!user) user = "user";

    char prompt[512];
    snprintf(prompt, sizeof(prompt), "%s@%s%c ",
             user, hostname, (getuid() == 0 ? '#' : '$'));

    char *ligne;
    while ((ligne = readline(prompt)) != NULL) {
        if (strlen(ligne) > 0) {
            add_history(ligne);
            write_history(".biceps_history");

            char *ptr = ligne;
            char *seq;
            while ((seq = strsep(&ptr, ";")) != NULL) {
                int n = analyse_com(seq);
                if (n > 0) {
#ifdef TRACE
                    printf("[TRACE] Execution : %s\n", mots[0]);
#endif
                    if (!exec_com_int(n, mots))
                        exec_com_ext(mots);
                    for (int i = 0; i < n; i++) free(mots[i]);
                }
            }
        }
        free(ligne);
    }

    printf("\n");
    cmd_sortie(0, NULL);
    return 0;
}
