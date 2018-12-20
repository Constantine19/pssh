#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "builtin.h"
#include "parse.h"

#define READ_SIDE 0
#define WRITE_SIDE 1
/*******************************************
 * Set to 1 to view the command line parse *
 *******************************************/
#define DEBUG_PARSE 0
#define PINFO_SIZE 20
#define ARRAY_SIZE 200
void print_banner ()
{
    printf ("                    ________   \n");
    printf ("_________________________  /_  \n");
    printf ("___  __ \\_  ___/_  ___/_  __ \\ \n");
    printf ("__  /_/ /(__  )_(__  )_  / / / \n");
    printf ("_  .___//____/ /____/ /_/ /_/  \n");
    printf ("/_/ Type 'exit' or ctrl+c to quit\n\n");
}


/* returns a string for building the prompt
 *
 * Note:
 *   If you modify this function to return a string on the heap,
 *   be sure to free() it later when appropirate!  */

void print_jobs(int arg,int grp_pid);
void set_fg_pgid (pid_t pgid);
void safe_print (char* str);
void change_status (int chld_pid,int stat,int op);
int return_info (int chld_pid, int output);

static pid_t pssh_id; 

static char* build_prompt ()
{
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    return  strcat(cwd,"/pssh$ ");
}

/* return true if command is found, either:
 *   - a valid fully qualified path was supplied to an existing file
 *   - the executable file was found in the system's PATH
 * false is returned otherwise */
static int command_found (const char* cmd)
{
    char* dir;
    char* tmp;
    char* PATH;
    char* state;
    char probe[PATH_MAX];
    int ret = 0;
    if (access (cmd, F_OK) == 0){
        return 1;
    }
    PATH = strdup (getenv("PATH"));
    for (tmp=PATH; ; tmp=NULL) {
        dir = strtok_r (tmp, ":", &state);
        if (!dir)
            break;

        strncpy (probe, dir, PATH_MAX);
        strncat (probe, "/", PATH_MAX);
        strncat (probe, cmd, PATH_MAX);

        if (access (probe, F_OK) == 0) {
            ret = 1;
            break;
        }
    }
    free (PATH);
    return ret;
}


static char* path_found (const char* cmd)
{
    char* dir;
    char* tmp;
    char* PATH;
    char* state;
    static char probe[PATH_MAX];
    PATH = strdup (getenv("PATH"));

    for (tmp=PATH; ; tmp=NULL) {
        dir = strtok_r (tmp, ":", &state);
        if (!dir)
            break;

        strncpy (probe, dir, PATH_MAX);
        strncat (probe, "/", PATH_MAX);
        strncat (probe, cmd, PATH_MAX);

        if (access (probe, F_OK) == 0) {
            break;
        }
    }
    return probe;
}

struct Job pinfo[ARRAY_SIZE];
struct Jobs pinfos[ARRAY_SIZE];

//pinfo[t].name
//pinfo[t].pids, pinfo[t].pgid,pinfo[t].npids,pinfo[t].status

void handler(int sig){
    printf("Caught Signal: %s\n", strsignal(sig));
}

// This method handles SIGCHLD signal
void child_handler(int sig)
{
    pid_t chld_pid;
    int status;
    


    while ((chld_pid=waitpid(-1,&status,WNOHANG|WUNTRACED|WCONTINUED)) > 0) 
    {
        fflush(stdout);

        int pgid=return_info(chld_pid,2);
        int npids=return_info(chld_pid,3);
        int k1=return_info(chld_pid,4);
        int ret_status=return_info(chld_pid,1);

        if (WSTOPSIG(status)==SIGTSTP) {
            kill(-return_info(chld_pid,2),SIGTSTP);
            set_fg_pgid(pssh_id);
            change_status(chld_pid,0,0);
            continue;
        }
        else if (WIFCONTINUED(status)) {
            change_status(chld_pid,2,0);
            print_jobs(2,return_info(chld_pid,2));
            continue;
        }
        else if (WIFSIGNALED(status)){
            change_status(chld_pid,1,0);
            set_fg_pgid(pssh_id);
            continue;
        }
        else if (WIFEXITED(status))
        {
            // This is "bg"
            if(ret_status==2)
            {
                change_status(chld_pid,1,0);
                printf("\n");
                print_jobs(2,pgid);
            }
            if (chld_pid==pinfos[k1].pids[npids-1])
            {
                change_status(chld_pid,1,0);
                set_fg_pgid(pssh_id);
            }
            continue;
        }
        else{
            set_fg_pgid(pssh_id);
            continue;
        }
    }
}
/* Called upon receiving a successful parse.
 * This function is responsible for cycling through the
 * tasks, and forking, executing, etc as necessary to get
 * the job done! */

void execute_tasks (Parse* P)
{
    unsigned int t;
    pid_t pid[ARRAY_SIZE];
    int fo,fi;
    int i = 0, j = 0;
    int pipefds[2*P->ntasks];
    int commd_found=1;
    //piping
    for( i = 0; i < P->ntasks; i++ ){
        if( pipe(pipefds + i*2) < 0 ){
            perror("Error at pipe\n");
            exit(EXIT_FAILURE);
        }
    }
    for (t = 0; t < P->ntasks; t++) {
        if (is_builtin (P->tasks[t].cmd)){
            if (!strcmp (P->tasks[t].cmd, "which")){
                if (!strcmp (P->tasks[t].argv[1], "which") || !strcmp (P->tasks[t].argv[1], "exit")){
                    if (P->outfile){
                        FILE *saved=stdout;
                        stdout=fopen(P->outfile,"w");
                        fprintf(stdout, "%s: shell built-in command\n",P->tasks[t].argv[1]);
                        fclose(stdout);
                        stdout=saved;
                    }
                    else{
                        fprintf(stdout, "%s: shell built-in command\n",P->tasks[t].argv[1]);
                    }
                }
                else
                {
                    if (P->outfile){
                        FILE *saved=stdout;
                        stdout=fopen(P->outfile,"w");
                        fprintf(stdout, "%s\n",path_found(P->tasks[t].argv[1]));
                        fclose(stdout);
                        stdout=saved;
                    }else{fprintf(stdout, "%s\n",path_found(P->tasks[t].argv[1]));}
                }                   
            }
            // This is "jobs"
            else if (!strcmp (P->tasks[t].cmd, "jobs")){
                print_jobs(0,0);
            }
            else if (!strcmp (P->tasks[t].cmd, "jobs2")){
                print_jobs(3,0);
            }
            // This is "fg"
            else if (!strcmp (P->tasks[t].cmd, "fg")){
                if (P->tasks[t].argv[1]==NULL){
                    printf("Usage: fg %%<job number>\n");
                }
                else{
                    char* fg_str=P->tasks[t].argv[1]+1;
                    if (!pinfos[atoi(fg_str)].pgid){
                        printf("pssh: invalid job number: %s\n",fg_str );
                    }
                    else{
                        kill(-pinfos[atoi(fg_str)].pgid,SIGCONT);
                        set_fg_pgid(pinfos[atoi(fg_str)].pgid);
                    }
                }
            }
            // This is "bg"
            else if (!strcmp (P->tasks[t].cmd, "bg"))
            {
                if (P->tasks[t].argv[1]==NULL)
                {
                    printf("Usage: bg %%<job number>\n");
                }
                else{
                    char* bg_str=P->tasks[t].argv[1]+1;
                    if (!pinfos[atoi(bg_str)].pgid){
                        printf("pssh: invalid job number: %s\n",bg_str );
                    }
                    else{

                        kill(-pinfos[atoi(bg_str)].pgid,SIGCONT);
                        
                    }
                }
            }
            // This handles "kill" command
            else if (!strcmp (P->tasks[t].cmd, "kill"))
            {
                if (P->tasks[t].argv[1]==NULL){
                    printf("Usage: kill [-s <signal>] <pid> | %% <job> ...\n");
                }
                if (strcmp(P->tasks[t].argv[1],"-s")){
                    char* kill_jobs=P->tasks[t].argv[1]+1;
                    char* kill_pid=P->tasks[t].argv[1];
                    if (strlen(P->tasks[t].argv[1])>3){

                        kill(-return_info(atoi(kill_pid),2),SIGINT);
                          
                    }
                    else{
                        printf("SEND SIGINT TO JOB %s\n", kill_jobs);
                        kill(-pinfos[atoi(kill_jobs)].pgid,SIGINT);
                    }
                }
                else{
                    char* kill_sig=P->tasks[t].argv[2];
                    char* kill_jobs=P->tasks[t].argv[3]+1;
                    char* kill_pid=P->tasks[t].argv[3];
                    if (strlen(P->tasks[t].argv[3])>3)
                    {

                        kill(-return_info(atoi(kill_pid),2),atoi(kill_sig));   
                    }
                    else{

                        kill(-pinfos[atoi(kill_jobs)].pgid,atoi(kill_sig));
                    }
                }
            }
            else{
                builtin_execute (P->tasks[t],P,&pinfo[0]);
            }
        }
        else if (command_found (P->tasks[t].cmd)) {
            pid[t]=vfork();
            setpgid(pid[t],pid[0]);
            pinfo[t].npids=P->ntasks;

            char str[80];
            strcpy(str,"");
            int k=0;
            while(P->tasks[t].argv[k])
            {
                strcat(str,P->tasks[t].argv[k]);
                strcat(str," ");
                k++;
            }
            pinfo[t].name=strdup(str);
            pinfo[t].pids=pid[t];
            pinfo[t].pgid=pid[0];

            if (P->background)
            {
                pinfo[t].status=BG;
            }
            else{
                pinfo[t].status=FG;   
            }
            if(pid[t]==0){
                // Implementation of "<"
                if (P->outfile){
                    fo = creat(P->outfile,S_IWUSR|S_IRUSR);
                    dup2(fo,STDOUT_FILENO);
                }
                // Implementation of ">"
                if(P->infile){
                    fi=open(P->infile,O_RDONLY);
                    dup2(fi,STDIN_FILENO);
                }
                if(t!=P->ntasks-1){
                    if(dup2(pipefds[j+1],1)<0){
                        perror("first dup2");
                        exit(EXIT_FAILURE);
                    }
                }
                if (j!=0 && j!=2*(P->ntasks)){
                        if (dup2(pipefds[j-2],0)<0) {
                            perror("second dup2");
                            exit(EXIT_FAILURE);
                        }
                    }
                for(i = 0; i < 2*P->ntasks; i++){
                    close(pipefds[i]);
                }
                if(execvp(P->tasks[t].cmd,P->tasks[t].argv)<0){
                    perror("error at execvp");
                    exit(EXIT_FAILURE);
                }               
            }
            else if(pid[t] < 0){
                perror("Fork error");
                exit(EXIT_FAILURE);
            }
            else{
                j+=2;
                if (j>2){
                    close(pipefds[j-3]);
                }
            }
        }
        else {
            printf ("pssh: command not found: %s\n", P->tasks[t].cmd);
            commd_found=0;
            break;
        }
    }

    if (!is_builtin (P->tasks[0].cmd) && commd_found){
        int jobs_increment=0;

        char name_str[80];
        strcpy(name_str,"");
        int stored=0;
        for (t = 0; t < P->ntasks; t++)
        {
            if(t>0)
                strcat(name_str,"| ");
            strcat(name_str,pinfo[t].name);
        }
        if (pinfo[0].status==FG)
        {
            set_fg_pgid(pinfo[0].pgid);
        }
        else if (pinfo[0].status==BG)
        {

            kill(-pinfo[0].pgid,SIGCONT);
        }

        while(pinfos[jobs_increment].name)
        {
            if (pinfos[jobs_increment].status==1){
                pinfos[jobs_increment].name=strdup(name_str);
                pinfos[jobs_increment].pgid=pinfo[0].pgid;
                pinfos[jobs_increment].status=pinfo[0].status;
                pinfos[jobs_increment].npids=pinfo[0].npids;
                for (t = 0; t < P->ntasks; t++) {
                    pinfos[jobs_increment].pids[t]=pinfo[t].pids;
                }
                stored=1;
                break;
            }
            jobs_increment++;
        }

        if (!stored)
        {
            pinfos[jobs_increment].name=strdup(name_str);
            pinfos[jobs_increment].pgid=pinfo[0].pgid;
            pinfos[jobs_increment].status=pinfo[0].status;
            pinfos[jobs_increment].npids=pinfo[0].npids;
            for (t = 0; t < P->ntasks; t++) 
            {
                pinfos[jobs_increment].pids[t]=pinfo[t].pids;
            }
        }

    }
}
void set_fg_pgid (pid_t pgid)
{
    void (*old)(int);

    old = signal (SIGTTOU, SIG_IGN);
    tcsetpgrp (STDIN_FILENO, pgid);
    tcsetpgrp (STDOUT_FILENO, pgid);
    signal (SIGTTOU, old);
}
void safe_print (char* str)
{
    pid_t fg_pgid;
    fg_pgid = tcgetpgrp (STDOUT_FILENO);
    set_fg_pgid (getpgrp());
    printf ("%s", str);
    set_fg_pgid (fg_pgid);
}
void handler_sigttou (int sig)
{
    while (tcgetpgrp(STDOUT_FILENO) != getpid ())
        pause ();
}


void change_status (int pid,int stat,int op){
    int k1=0;
    if (op==0)
    {
        while(pinfos[k1].pgid){
            if (pinfos[k1].pids[pinfos[k1].npids-1]==pid)
            {
                pinfos[k1].status=stat;
            }
            k1++;
        }
    }
    else if (op==1)
    {
        while(pinfos[k1].pgid)
        {
            if (pinfos[k1].pgid==pid)
            {
                pinfos[k1].status=stat;   
            }
        }
    }

}

int return_info (int chld_pid,int output)
{
    int k1=0;
    while(pinfos[k1].pgid)
    {
        int k2=0;
        while(pinfos[k1].pids[k2]){
            if (pinfos[k1].pids[k2]==chld_pid){
                if (output==1){
                    return pinfos[k1].status; 
                }
                else if (output==2){
                    return pinfos[k1].pgid;
                }   
                else if (output==3){
                    return pinfos[k1].npids;
                }
                else if (output==4){
                    return k1;
                }             
                else if (output==5){
                    return k2;
                }
            }
            k2++;
        }
        k1++;
    }
}

void print_jobs(int arg,int grp_pid){
    int k=0;
    if (arg==1)
    {
        printf("\nPGID:\tSTATUS\tNPIDS\tNAME\n");
        while(pinfos[k].pgid)
        {
            printf("%d\t%d\t%d\t%s\n",pinfos[k].pgid,pinfos[k].status,pinfos[k].npids,pinfos[k].name );
            k++;
        }
    }
    else if (arg==2)
    {
        while(pinfos[k].pgid){
            char* str;
            switch(pinfos[k].status){
                case 3:
                    str="running";
                    break;
                case 0:
                    str="stopped";
                    break;
                case 1:
                    str="done";
                    break;  
                case 2:
                    str="continued";
                    break;
                default:
                    str="undefined";
                    break; 
            }
            if (pinfos[k].pgid==grp_pid){
                printf("[%d] + %s\t%d\t%s\n",k,str,pinfos[k].pgid,pinfos[k].name);
            }
            k++;
        }
    }
    else if (arg==3)
    {
        printf("\nPGID:\tPID\tNAME\n");
        while(pinfos[k].pgid){
            int k2=0;
            while(pinfos[k].pids[k2]){
                printf("%d\t%d\t%s\n",pinfos[k].pgid,pinfos[k].pids[k2],pinfos[k].name );
                k2++;
            }
            k++;
        }
    }
    else{
        while(pinfos[k].pgid){
            char* str;
            switch(pinfos[k].status){
                case 3:
                    str="running";
                    break;
                case 0:
                    str="stopped";
                    break;
                case 1:
                    str="done";
                    break;  
                case 2:
                    str="running";
                    break;
                default:
                    str="undefined";
                    break; 
            }
            if (strcmp(str,"done")){
                printf("[%d] + %s\t\t%s\n",k,str,pinfos[k].name);
            }
            
            k++;
        }
    }
}

int main (int argc, char** argv)
{
    char* cmdline;
    Parse* P;
    print_banner ();

    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
    signal(SIGSEGV, handler);
    signal(SIGCONT, handler);
    signal(SIGTSTP, handler);
    signal(SIGCHLD,child_handler);
    signal(SIGTTIN,handler);
    signal(SIGTTOU,handler_sigttou);

    pssh_id=getpid();

    while (1) {
        cmdline = readline (build_prompt());
        if (!cmdline)
            exit (EXIT_SUCCESS);

        P = parse_cmdline (cmdline);
        if (!P)
            goto next;

        if (P->invalid_syntax) {
            printf ("pssh: invalid syntax\n");
            goto next;
        }

#if DEBUG_PARSE
        parse_debug (P);
#endif
        execute_tasks (P);
    next:
        parse_destroy (&P);
        free(cmdline);
    }
}
