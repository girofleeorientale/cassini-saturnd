# cassini
L'architecture du client cassini est relativement simple. Celui ci analyse les options de la ligne de commande qu'on lui passe, et selon la requête que l'utilisateur veut envoyer, va appeler la fonction auxiliaire correspondante dans le fichier `requests.c`. Ces fonctions se chargent d'envoyer la requête par le tube de requête, éventuellement de lire la réponse obtenue sur le tube de réponse, et d'afficher le résultat. Le chemin du dossier contenant les tubes nommés peut être passé avec l'option `-p`.

# saturnd
Le démon `saturnd`, par défaut, crée au démarrage le dossier `/tmp/<nom d'utilisateur>/saturnd`, contenant les dossiers `pipes` et `tasks`.

- Il crée dans le dossier `pipes` deux tubes nommés, `saturnd-request-pipe` et `saturnd-reply-pipe`. Ces tubes seront utilisés pour communiquer avec le client `cassini`.
- Le dossier `tasks` sera utilisé pour stocker sur le disque les tâches que saturnd doit exécuter. Ce dernier contient un fichier `task_count`, qui permet de stocker l'identifiant de la dernière tâche créée, afin de pouvoir assigner un identifiant valides aux nouvelles tâches après un redémarrage, ainsi que chaque tâche, représentée par un sous dossier, nommé comme l'identifiant de la tâche. Ces dossiers contiennent
    - `argc`, contenant le nombre d'arguments de la commande que la tâche doit lancer
    - `argv`, contenant chaque argument de la commande, précédés de leur taille
    - `execs`, contenant le nombre de fois que la tâche s'est déjà executée, ainsi que pour chaque exécution, l'heure de cette dernière ainsi que son code de sortie
    - `stdout`, contenant la sortie standard de la dernière exécution de la tâche
    - `stderr`, contenant l'erreur standard de la dernière exécution de la tâche
    - `timing`, contenant les champs de la structure `timing` fournie, a savoir des champs de bits 
    représentants les minutes, les heures et les jours de la semaine où la tâche doit s'exécuter

Il est possible de passer des chemins personnalisés pour `pipes` et pour `tasks` avec les options `-p` et `-t`.

## La gestion des tuyaux
Afin de simplifier l'utilisation des tuyaux, et de pouvoir réutiliser facilement du code entre `saturnd` et `cassini`, un système à été mis en place
- Une structure `Pipes` contient une référence a un tube de requête, et un tube de réponse. Cette dernière est déclarée dans `utils.h`.
- Deux variables globales, `req_pipe_path` et `res_pipe_path`, sont déclarées également dans `utils.h`, et vont contenir les chemins du tube de requête et du tube de réponse.
- Une fonction `int open_pipe(struct Pipes *pipes, int id, int mode)` prend un pointeur vers la structure `Pipes`, un type de tube, et un mode d'ouverture. Les types de tubes sont `REQUEST_PIPE_ID` et `REPLY_PIPE_ID`, et les modes d'ouvertures sont `PIPE_OPEN_MODE_CASSINI` et `PIPE_OPEN_MODE_SATURND`. Cette fonction peut être réutilisée dans les deux programmes, et ouvre le tube de requete en lecture dans `saturnd` et en écriture dans `cassini`, et inversement pour le tube de réponse.

## La structure Task
Pour simplifier la gestion des taches en mémoire, dans le code, ainsi que leur écriture et lecture du disque, une structure `Task` fut créée. Cette dernière est déclarée dans `tasks.h` et comporte des champs pour toutes les informations nécéssaires au bon déroulement du stockage ou de l'exécution d'une tâche. Le fichier `tasks.c` comporte des fonctions utilitaires simplifiant leur utilisation, à savoir

- `int write_task_to_disk(struct Task *task, char *taskdir)` :  cette fonction prend un pointeur vers une tâche ainsi que le chemin du dossier `tasks`, et écrit toutes les informations de cette tâche dans son dossier (en le crééant si il n'existe pas). Elle retourne 0 en cas de succès ou -1 en cas d'erreur.
- `struct Task * read_task_from_disk(char *tasks_dir, uint64_t taskid)` : cette fonction prend le chemin du dossier `tasks` ainsi qu'un identifiant de tâche, et retourne un pointeur vers une tâche contenant toutes les informations lues du disque
- `struct Task ** load_all_tasks(char *task_dir, uint32_t *taskcount)` : cette fonction prend le chemin du dossier `tasks` ainsi qu'un pointeur vers un entier. Elle va lire toutes les tâches présentes dans le dossier, puis retourner un pointeur vers une liste de tâches. La longueur de cette liste sera écrite dans taskcount.
- `void free_task(struct Task *task)` : permet la libération de la mémoire de tous les champs d'une tâche, ainsi que de la tâche elle même.

Cette structure ainsi que les fonctions utilitaires sont utilisées partout ou des tâches sont lues ou modifiées (sauf pour la suppression, qui ne consiste qu'à supprimer le dossier de la tâche ainsi que ses fichiers)

## Le task launcher
`saturnd` va donner naissance à un sous processus, qu'on appelera `task launcher`. Le rôle de ce dernier sera de lancer les tâches aux bons moments. Pour ce faire, il attend le début de chaque minute, en se servant des fonctions `sleep` et `localtime` pour calculer le temps à attendre, puis recharge les tâches depuis le disque. Pour chaque tâche, il vérifie qu'elle doit bien être lancée au moment présent, en se servant de son champ `timing`. Si oui, alors il démarre un nouveau sous processus, qui va dupliquer sa sortie et son erreur standard dans les fichiers `stdout` et `stderr` de la tâche avec `dup2`, puis effectuer un recouvrement grâce à `execvp`. Le `task launcher` va attendre la fin de l'exécution de cette tâche, puis noter l'heure et le code de sortie dans son fichier `execs`.

## Le gestionnaire de requêtes
Pendant que le `task launcher` tourne en arrière-plan, le processus principal de saturnd va se charger de gérer les requêtes du client. Pour cela, il va effectuer une lecture bloquante sur le tube de requête, et récupérer la requête du client `cassini` lorsqu'elle arrive.

Il va ensuite appeler la fonction adéquate de `requests-handler.c`, qui a une fonction pour chaque type de requête. C'est ces fonctions qui vont prendre le relai pour lire la fin de la requête (si il y en a une), effecuter les actions et récupérer les informations correspondantes, notamment grâce a la structure `Task` et les fonctions auxiliaires qui l'accompagnent, et transmettre la réponse à `cassini`. Ces fonctions retournent 0 en cas de succès, auquel cas `saturnd` va se mettre en attente de la requête suivante, ou -1 en cas d'erreur, auquel cas `saturnd` va sortir de la boucle infini, faire le nettoyage mémoire nécessaire, envoyer un signal de terminaison au `task launcher`, et quitter avec un code d'erreur lui même.

La requête de terminaison est la seule exception. Elle n'a pas de fonction auxiliaire, elle quitte simplement la boucle, arrête le `task launcher` et fait également du nettoyage mémoire, et cette fois ci `saturnd` quitte avec un code de succès.

Le gestionnaire de requêtes ET le task launcher implémentent tout les deux un `sigaction` qui attend l'arrivée du signal de terminaison `SIGTERM`. Cela permet au task launcher de faire du nettoyage mémoire lorsque le processus principal l'interrompt, et cela permet au processus principal de faire du nettoyage si il est arrêté autrement que par la requête de terminaison envoyée par `cassini` (par un kill extérieur, par exemple).