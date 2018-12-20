#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "builtin.h"
#include "parse.h"

static char* builtin[] = {
    "exit",
    "fg",
    "bg",
    "kill",
    "jobs",
    "jobs2",
    NULL
};

int is_builtin (char* cmd)
{
    int i;
    for (i=0; builtin[i]; i++) {
        if (!strcmp (cmd, builtin[i]))
            return 1;
    }
    return 0;
}


void builtin_execute (Task T,Parse* P,void* arg)
{

    if (!strcmp (T.cmd, "exit")) {
        exit (EXIT_SUCCESS);
    }
    else if (!strcmp (T.cmd, "fg")) {
        if (P->tasks[0].argv[1]==NULL)
        {
            printf("Usage: fg %%<job number>\n");
        }
        
    }
    else if (!strcmp (T.cmd, "bg")) {
        if (P->tasks[0].argv[1]==NULL)
        {
            printf("Usage: bg %%<job number>\n");
        }
        
    }
    else if (!strcmp (T.cmd, "kill")) {
        if (P->tasks[0].argv[1]==NULL)
        {
            printf("Usage: kill [-s <signal>] <pid> | %%<job> ...\n");
        }
        
    }
    else if (!strcmp (T.cmd, "jobs")) {
        printf("Summary:\nt\tname\tpid\tgpids\tnpids\n");
        
    }
    else {
        printf ("pssh: builtin command: %s (not implemented!)\n", T.cmd);
    }
}
