# TP3_OS

NOM: [SOLTAN]  
PRENOM: [Sarra]

# **Biceps v3 – Interpréteur de commandes (Polytech Sorbonne)**

Ce projet est une application de messagerie en ligne de commande qui permet aux utilisateurs de communiquer entre eux sur un réseau local à l’aide du protocole **BEUIP** (basé sur UDP).

Cette version 3 améliore le programme en ajoutant du **multi-threading**, ce qui permet d’envoyer et de recevoir des messages en même temps sans bloquer l’exécution du programme.

---

## **Fonctionnement**

Le programme repose sur **deux threads principaux** :

- **Le thread principal** : gère les commandes saisies par l’utilisateur (envoi de messages, affichage, etc.)
- **Le thread serveur UDP** : écoute en permanence sur le port **9998** pour recevoir les informations réseau (connexions, messages, déconnexions)

Ce fonctionnement permet une communication en temps réel, même pendant la saisie de commandes.

---

## **Gestion des contacts**

Les utilisateurs connectés sont stockés dans une **liste chaînée triée par ordre alphabétique**.

Comme cette liste est utilisée par plusieurs threads, un **mutex (`contacts_mutex`)** est utilisé pour éviter les problèmes d’accès concurrent (**race conditions**).

---

## **Sécurité**

Dans les versions précédentes, le programme utilisait des échanges réseau internes (**localhost**) pour certaines commandes, ce qui pouvait poser des problèmes de sécurité.

Avec cette version, tout est géré dans le **même processus grâce aux threads**.  
Cela permet d’éviter les communications inutiles sur le réseau local et limite les risques d’attaques.

---

## **Structure du projet**

- **biceps.c** : contient le programme principal, la gestion des threads et des contacts  
- **creme.h** : contient les structures de données et les constantes  
- **Makefile** : permet de compiler le projet avec les options **-Wall -Werror**

---

## **Compilation**

Pour compiler le projet :

```bash
make
