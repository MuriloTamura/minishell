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
 *   read()    - leitura do terminal (via STDIN_FILENO)
 *   write()   - escrita no terminal (via STDOUT_FILENO)
 *   open()    - abre arquivo para redirecionamento
 *   close()   - fecha descritor de arquivo
 *   dup2()    - redireciona stdin/stdout para arquivo
 *   mkdir()   - cria diretorio (built-in mkdir)
 *   rmdir()   - remove diretorio vazio (built-in rmdir)
 *   exit()    - encerra o processo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#define TRUE         1
#define MAX_LINE   512
#define MAX_ARGS    64
#define DELIMITERS " \t\n"

typedef struct {
    char  *argv[MAX_ARGS];
    int    argc;
    char  *redir_in;
    char  *redir_out;
    char  *redir_app;
} Command;

void type_prompt(void)
{
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("\033[1;32mminishell\033[0m:\033[1;34m%s\033[0m$ ", cwd);
    else
        printf("minishell$ ");
    fflush(stdout);
}

int read_command(Command *cmd)
{
    static char buffer[MAX_LINE];
    int     i = 0;
    char    c;
    ssize_t n;

    memset(cmd, 0, sizeof(Command));

    while (i < (int)sizeof(buffer) - 1) {
        n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) return -1;
        if (c == '\n') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';

    if (i == 0) return 0;

    char *token = strtok(buffer, DELIMITERS);
    while (token != NULL) {
        if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, DELIMITERS);
            if (token) cmd->redir_app = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, DELIMITERS);
            if (token) cmd->redir_out = token;
        } else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, DELIMITERS);
            if (token) cmd->redir_in = token;
        } else {
            if (cmd->argc < MAX_ARGS - 1)
                cmd->argv[cmd->argc++] = token;
        }
        token = strtok(NULL, DELIMITERS);
    }
    cmd->argv[cmd->argc] = NULL;
    return 0;
}

/*
 * apply_redirections
 * Redireciona stdin/stdout do processo filho usando:
 *   open()  - abre/cria o arquivo
 *   dup2()  - copia fd para STDIN_FILENO ou STDOUT_FILENO
 *   close() - fecha o fd original apos duplicar
 */
void apply_redirections(Command *cmd)
{
    int fd;

    /* < arquivo : redireciona entrada */
    if (cmd->redir_in) {
        fd = open(cmd->redir_in, O_RDONLY);
        if (fd < 0) { perror(cmd->redir_in); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    /* > arquivo : redireciona saida (sobrescreve) */
    if (cmd->redir_out) {
        fd = open(cmd->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(cmd->redir_out); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    /* >> arquivo : redireciona saida (append) */
    if (cmd->redir_app) {
        fd = open(cmd->redir_app, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) { perror(cmd->redir_app); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

int handle_builtin(Command *cmd)
{
    if (cmd->argc == 0) return 0;
    char *command = cmd->argv[0];

    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        printf("Encerrando o shell. Ate mais!\n");
        exit(0);
    }

    if (strcmp(command, "cd") == 0) {
        const char *path = cmd->argv[1];
        if (!path) {
            path = getenv("HOME");
            if (!path) path = "/";
        }
        if (chdir(path) != 0)
            perror("cd");
        return 1;
    }

    if (strcmp(command, "ls") == 0) {
        const char *path = cmd->argv[1] ? cmd->argv[1] : ".";
        DIR *dir = opendir(path);
        struct dirent *entry;

        if (dir == NULL) {
            perror("ls");
            return 1;
        }

        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            printf("%s\n", entry->d_name);
        }

        closedir(dir);
        return 1;
    }

    /*
     * mkdir [dir] - cria um diretorio
     * Chamada de sistema: mkdir(path, mode)
     *   mode 0755 = rwxr-xr-x (dono tem tudo, outros podem entrar e listar)
     */
    if (strcmp(command, "mkdir") == 0) {
        if (cmd->argv[1] == NULL) {
            fprintf(stderr, "mkdir: falta o nome do diretorio\n");
            return 1;
        }
        /* percorre todos os argumentos: mkdir a b c cria tres diretorios */
        for (int i = 1; cmd->argv[i] != NULL; i++) {
            if (mkdir(cmd->argv[i], 0755) != 0)
                perror(cmd->argv[i]);
            else
                printf("Diretorio '%s' criado\n", cmd->argv[i]);
        }
        return 1;
    }

    /*
     * rmdir [dir] - remove um diretorio VAZIO
     * Chamada de sistema: rmdir(path)
     * Falha se o diretorio nao estiver vazio (use 'rm -r' para isso)
     */
    if (strcmp(command, "rmdir") == 0) {
        if (cmd->argv[1] == NULL) {
            fprintf(stderr, "rmdir: falta o nome do diretorio\n");
            return 1;
        }
        for (int i = 1; cmd->argv[i] != NULL; i++) {
            if (rmdir(cmd->argv[i]) != 0)
                perror(cmd->argv[i]);
            else
                printf("Diretorio '%s' removido\n", cmd->argv[i]);
        }
        return 1;
    }

    if (strcmp(command, "help") == 0) {
        printf("=== minishell - comandos internos ===\n");
        printf("  cd [dir]       : muda de diretorio         (chdir)\n");
        printf("  ls [dir]       : lista arquivos/diretorios (opendir/readdir)\n");
        printf("  mkdir [dir]    : cria diretorio            (mkdir)\n");
        printf("  rmdir [dir]    : remove diretorio vazio    (rmdir)\n");
        printf("  exit / quit    : encerra o shell           (exit)\n");
        printf("  help           : esta mensagem\n");
        printf("\n=== redirecionamentos suportados ===\n");
        printf("  cmd > arq      : redireciona saida (sobrescreve)\n");
        printf("  cmd >> arq     : redireciona saida (append)\n");
        printf("  cmd < arq      : redireciona entrada\n");
        printf("  cmd < in > out : entrada e saida simultaneos\n");
        return 1;
    }

    return 0;
}

int main(void)
{
    Command cmd;
    int     status;
    pid_t   pid;

    printf("=== minishell iniciado (digite 'help' ou 'exit') ===\n");

    while (TRUE) {

        type_prompt();

        if (read_command(&cmd) == -1) {
            printf("\nEOF detectado. Saindo...\n");
            break;
        }

        if (cmd.argc == 0) continue;

        if (handle_builtin(&cmd)) continue;

        /* fork() - cria processo filho */
        pid = fork();

        if (pid != 0) {
            /* Processo PAI: aguarda filho */
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                printf("[minishell] processo filho terminou com codigo %d\n",
                       WEXITSTATUS(status));
        } else {
            /* Processo FILHO: aplica redirecionamentos e executa */
            apply_redirections(&cmd);
            execvp(cmd.argv[0], cmd.argv);
            perror(cmd.argv[0]);
            exit(1);
        }
    }

    return 0;
}
