#include <stdio.h>
#include <stdlib.h>
int main(int,char **);
int __getmainargs(int *,char ***,char ***,int,void *);
_Noreturn void host_assert(const char *expr,const char *file,int line) {
    printf("ASSERT FAILED: %s:%d: %s\n",file,line,expr); _exit(1);
}
void host_entry(void) {
    int argc=0, mode=0; char **argv=0, **env=0;
    if (__getmainargs(&argc,&argv,&env,0,&mode)) _exit(2);
    _exit(main(argc,argv));
}
