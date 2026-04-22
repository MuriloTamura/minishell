/*
 * minishell.c
 * Shell simplificado baseado no pseudocodigo do Tanenbaum
 * (Sistemas Operacionais Modernos, Figura 1.19)
 *
 * Chamadas de sistema utilizadas:
 *   fork()    - cria processo filho
 *   execvp()  - executa comando no processo filho
 *   waitpid() - processo pai aguarda filho terminar
 *   getcwd()  - obtem diretorio atual (para o prompt)
 *   chdir()   - muda de diretorio (comando interno cd)
 *   write()   - escrita no terminal (via STDOUT_FILENO)
 *   read()    - leitura do terminal  (via STDIN_FILENO)
 *   exit()    - encerra o processo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* fork, execvp, getcwd, chdir, read, write */
#include <sys/types.h>   /* pid_t                                     */
#include <sys/wait.h>    /* waitpid, WIFEXITED, WEXITSTATUS           */

/* ------------------------------------------------------------------ */
/*  Constantes                                                          */
/* ------------------------------------------------------------------ */
#define TRUE          1
#define MAX_INPUT   512          /* tamanho maximo da linha de comando */
#define MAX_ARGS     64          /* numero maximo de argumentos        */
#define DELIMITERS  " \t\n"     /* separadores de tokens              */

/* ------------------------------------------------------------------ */
/*  type_prompt  -  exibe o prompt na tela                             */
/*  Equivalente a type_prompt() do pseudocodigo do Tanenbaum           */
/* ------------------------------------------------------------------ */
void type_prompt(void)
{
    char cwd[512];

    /* getcwd() - chamada de sistema que obtem o diretorio corrente */
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        /* Destaca usuario@minishell e diretorio atual */
        printf("\033[1;32mminishell\033[0m:\033[1;34m%s\033[0m$ ", cwd);
    } else {
        printf("minishell$ ");
    }

    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  read_command  -  le a linha digitada e a divide em tokens          */
/*  Equivalente a read_command(command, parameters) do Tanenbaum       */
/*                                                                      */
/*  Retorna:  0 se leu um comando, -1 em caso de EOF ou erro           */
/* ------------------------------------------------------------------ */
int read_command(char *command, char **parameters)
{
    char buffer[MAX_INPUT];
    int  i = 0;
    char c;
    ssize_t n;

    /* Usa a chamada de sistema read() sobre STDIN_FILENO */
    while (i < (int)sizeof(buffer) - 1) {
        n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) return -1;   /* EOF ou erro */
        if (c == '\n') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';

    /* Linha vazia: nada a executar */
    if (i == 0) {
        command[0]    = '\0';
        parameters[0] = NULL;
        return 0;
    }

    /* Tokeniza a linha usando strtok */
    int argc = 0;
    char *token = strtok(buffer, DELIMITERS);
    while (token != NULL && argc < MAX_ARGS - 1) {
        parameters[argc++] = token;
        token = strtok(NULL, DELIMITERS);
    }
    parameters[argc] = NULL;   /* execvp exige vetor terminado em NULL */

    /* O primeiro token e o proprio comando */
    strncpy(command, parameters[0], MAX_INPUT - 1);
    command[MAX_INPUT - 1] = '\0';

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Comandos internos (built-ins)                                       */
/*  Precisam rodar no proprio processo do shell, nao em filho           */
/* ------------------------------------------------------------------ */

/* Retorna 1 se o comando foi tratado como built-in, 0 caso contrario */
int handle_builtin(char *command, char **parameters)
{
    /* --- exit / quit -------------------------------------------- */
    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        printf("Encerrando o shell. Ate mais!\n");
        exit(0);
    }

    /* --- cd ----------------------------------------------------- */
    if (strcmp(command, "cd") == 0) {
        const char *path = parameters[1];
        if (path == NULL) {
            /* cd sem argumento vai para $HOME */
            path = getenv("HOME");
            if (path == NULL) path = "/";
        }
        /* chdir() - chamada de sistema para mudar de diretorio */
        if (chdir(path) != 0) {
            perror("cd");
        }
        return 1;
    }

    /* --- help --------------------------------------------------- */
    if (strcmp(command, "help") == 0) {
        printf("=== minishell - comandos internos ===\n");
        printf("  cd [dir]  : muda de diretorio\n");
        printf("  exit/quit : encerra o shell\n");
        printf("  help      : exibe esta mensagem\n");
        printf("Qualquer outro comando e executado via execvp().\n");
        return 1;
    }

    return 0;   /* nao e built-in */
}

/* ------------------------------------------------------------------ */
/*  main  -  loop principal do shell                                    */
/*  Segue EXATAMENTE o pseudocodigo do Tanenbaum (Figura 1.19)         */
/* ------------------------------------------------------------------ */
int main(void)
{
    char   command[MAX_INPUT];
    char  *parameters[MAX_ARGS];
    int    status;
    pid_t  pid;

    printf("=== minishell iniciado (digite 'help' ou 'exit') ===\n");

    while (TRUE) {                          /* repita para sempre */

        type_prompt();                      /* mostra prompt na tela */

        if (read_command(command, parameters) == -1) {
            /* EOF (Ctrl+D): encerra o shell graciosamente */
            printf("\nEOF detectado. Saindo...\n");
            break;
        }

        /* Linha vazia: volta ao inicio do loop */
        if (command[0] == '\0') continue;

        /* Trata comandos internos antes de criar filho */
        if (handle_builtin(command, parameters)) continue;

        /* -------------------------------------------------------- */
        /*  fork() - cria processo filho                             */
        /* -------------------------------------------------------- */
        pid = fork();

        if (pid != 0) {
            /* ----------------------------------------------------- */
            /*  Codigo do processo PAI                                */
            /* ----------------------------------------------------- */

            /* waitpid() - aguarda o processo filho acabar           */
            waitpid(pid, &status, 0);

            /* Informa codigo de saida caso diferente de zero */
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                printf("[minishell] processo filho terminou com codigo %d\n",
                       WEXITSTATUS(status));
            }

        } else {
            /* ----------------------------------------------------- */
            /*  Codigo do processo FILHO                              */
            /* ----------------------------------------------------- */

            /*
             * execvp() - executa o comando
             * Diferenca em relacao ao execve() do Tanenbaum:
             *   execvp busca o executavel no PATH automaticamente,
             *   dispensando o caminho absoluto.
             *   Assinatura: execvp(command, parameters)
             */
            execvp(command, parameters);

            /*
             * Se execvp retornar, houve erro (comando nao encontrado).
             * O filho deve sair para nao duplicar o loop do shell.
             */
            perror(command);
            exit(1);
        }
    }

    return 0;
}