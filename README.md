SOLTAN 
Sarra

# biceps v3 – Bel Interpréteur de Commandes des Élèves de Polytech Sorbonne

## Structure du code

```
.
├── biceps.c    # Shell interactif (TP1) + commandes internes beuip
├── beuip.c     # Serveur UDP (thread), protocole BEUIP, liste chaînée
├── beuip.h     # Interface publique du module beuip
├── Makefile
└── README.md
```

## Ce que nous avons fait

### TP1 – Shell `biceps`
- Lecture de ligne avec `readline` (historique persistant dans `.biceps_history`)
- Analyse de commande avec `strsep` (séparateurs espace/tabulation)
- Commandes internes : `exit`, `cd`, `pwd`, `vers`, `beuip`
- Exécution de commandes externes via `fork` + `execvp` + `waitpid`
- Commandes séquentielles séparées par `;`
- `SIGINT` ignoré (Control-C ne tue pas le shell)
- Compilation conditionnelle `-DTRACE` pour les traces de débogage

### TP3 – Protocole BEUIP avec multi-threading

Le serveur UDP tourne dans un **thread dédié** (POSIX `pthread`) au sein du même processus que le shell. Cela permet :
- Le partage direct de la liste chaînée des utilisateurs (pas de message vers soi-même)
- La suppression des codes `3`, `4`, `5` du serveur UDP (sécurité renforcée)

#### Liste chaînée (`struct elt`)
- Maintenue triée alphabétiquement par pseudo
- Protégée par un `pthread_mutex_t` (accès concurrent shell / serveur)
- Fonctions : `ajoute_elt`, `supprime_elt`, `libere_liste`

#### Protocole BEUIP
| Code | Description |
|------|-------------|
| `0`  | Départ d'un utilisateur (sans AR) |
| `1`  | Identification broadcast |
| `2`  | AR d'identification |
| `9`  | Message privé ou broadcast (sans AR) |

Les codes `3`, `4`, `5` ne sont plus traités par le serveur UDP.

#### Commandes `beuip`
```
beuip start <pseudo>         # Lance le serveur UDP
beuip stop                   # Arrête le serveur (envoie '0' à tous)
beuip list                   # Affiche la liste des utilisateurs
beuip message <user> <msg>   # Envoie un MP à un utilisateur
beuip message all <msg>      # Envoie un message à tous
```

## Compilation et utilisation

```bash
make                  # Produit ./biceps
make memory-leak      # Produit ./biceps-memory-leaks (avec -g -O0)
make clean            # Supprime les binaires et .o

# Vérification fuites mémoire :
valgrind --leak-check=full --track-origins=yes --errors-for-leak-kinds=all \
         --error-exitcode=1 ./biceps-memory-leaks
```

## Adresse broadcast

L'adresse broadcast est définie dans `beuip.h` :
```c
#define BROADCAST_ADDR "192.168.88.255"
```
Elle peut être surchargée à la compilation avec `-DBROADCAST_ADDR=\"...\"`
