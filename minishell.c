#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>

    pid_t pid_en_premier_plan = 0;

    void traitement(int sig) {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
            if (pid == pid_en_premier_plan) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    pid_en_premier_plan = -1;// signale qu’il est fini
                }
            }
            if (WIFEXITED(status)) {
                printf("\n[%d] Terminé avec statut %d\n", pid, WEXITSTATUS(status));
            }
            if (WIFSIGNALED(status)) {
                printf("\n[%d] Interrompu par signal %d\n", pid, WTERMSIG(status));
            }
            if (WIFSTOPPED(status)) {
                printf("\n[%d] Arrêté avec statut %d\n", pid, WEXITSTATUS(status));
            }
            if (WIFCONTINUED(status)) {
                printf("\n[%d] Repris avec statut %d\n", pid, WEXITSTATUS(status));
            }
        }
    }

    sigset_t masque_globale; // Création de l'ensemble des signaux que l'on peut masquer

    void init_signal() {
        sigemptyset(&masque_globale); //Mise à 0 de l'ensemble des masques
        sigaddset(&masque_globale, SIGINT);  //Ajout de Ctrl+C
        sigaddset(&masque_globale, SIGTSTP); //Ajout de Ctrl+Z
        sigprocmask(SIG_BLOCK, &masque_globale, NULL); // Bloquage de ces masques
    }

    void change_directory(const char *chemin) {
        const char *cible = chemin ? chemin : getenv("HOME");
        if (chdir(cible) != 0) {
            perror("Erreur lors du changement de répertoire");
        } else {
            char *cwd = getcwd(NULL, 0);
            if (cwd != NULL) {
                printf("Répertoire courant : %s\n", cwd);
                free(cwd);
            }
        }
    }

    void lister_contenu_repertoire(const char *chemin) {
        if (chemin == NULL) chemin = "."; //repertoire courant
        DIR *dir = opendir(chemin);
        if (dir == NULL) {
            perror("Erreur ouverture du répertoire");
            return;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            printf("%s\n", entry->d_name);
        }
        closedir(dir);
    }

    int main(void) {
        bool fini = false;
        struct sigaction sa;
        sa.sa_handler = traitement;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(SIGCHLD, &sa, NULL) == -1) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
        init_signal();

        while (!fini) {
            printf("> ");
            struct cmdline *commande = readcmd();
            if (commande == NULL) {
                perror("erreur lecture commande");
                exit(EXIT_FAILURE);
            } else if (commande->err) {
                printf("erreur saisie de la commande : %s\n", commande->err);
            } else {
                int nb_cmds = 0;
                while (commande->seq[nb_cmds] != NULL) nb_cmds++; //compte le nombre de commande
                int prev_fd = -1;

                for (int i = 0; i < nb_cmds; i++) {
                    int pipe_fd[2];
                    if (i < nb_cmds - 1) {
                        if (pipe(pipe_fd) == -1) {
                            perror("pipe");
                            exit(EXIT_FAILURE);
                        }
                    }

                    char **cmd = commande->seq[i];

                    if (cmd[0]) {
                        if (strcmp(cmd[0], "exit") == 0) {
                            fini = true;
                            printf("Au revoir ...\n");
                            break;
                        } else if (strcmp(cmd[0], "chdir") == 0) {
                            change_directory(cmd[1]);
                            continue;
                        } else if (strcmp(cmd[0], "dir") == 0) {
                            lister_contenu_repertoire(cmd[1]);
                            continue;
                        }

                        pid_t pid = fork();
                        if (pid == -1) {
                            perror("fork");
                            exit(EXIT_FAILURE);
                        }
                        if (pid == 0) {
                            sigprocmask(SIG_UNBLOCK, &masque_globale, NULL);

                            if (i == 0 && commande->in) {
                                int fd_in = open(commande->in, O_RDONLY);
                                if (fd_in == -1) { perror("open in"); exit(EXIT_FAILURE); }
                                dup2(fd_in, STDIN_FILENO);
                                close(fd_in);
                            }
                            if (i == nb_cmds - 1 && commande->out) {
                                int fd_out = open(commande->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                if (fd_out == -1) { perror("open out"); exit(EXIT_FAILURE); }
                                dup2(fd_out, STDOUT_FILENO);
                                close(fd_out);
                            }

                            if (prev_fd != -1) {
                                dup2(prev_fd, STDIN_FILENO);
                                close(prev_fd);
                            }
                            if (i < nb_cmds - 1) {
                                close(pipe_fd[0]);
                                dup2(pipe_fd[1], STDOUT_FILENO);
                                close(pipe_fd[1]);
                            }

                            execvp(cmd[0], cmd);
                            perror("execvp");
                            exit(EXIT_FAILURE);
                        } else {
                            if (commande->backgrounded == NULL) {
                                pid_en_premier_plan = pid;
                                while (pid_en_premier_plan > 0) pause();
                            } else {
                                printf("[%d] (en arrière-plan)\n", pid);
                            }
                        }
                    }
                    if (prev_fd != -1) close(prev_fd);
                    if (i < nb_cmds - 1) {
                        close(pipe_fd[1]);
                        prev_fd = pipe_fd[0];
                    }
                }
            }
        }
        return EXIT_SUCCESS;
}

