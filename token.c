#include<glib.h>
#include<stdio.h>
int main(int argc,char **argv){
FILE *f;
gchar ch;
gchar *str;

GDir *dir = g_dir_open(argv[1],0,NULL);
g_chdir(argv[1]);

while((str = GINT_TO_POINTER(g_dir_read_name(dir))) !=NULL){
f = fopen(str,"r");
g_printf("[%s]\n",str);
	while((ch = fgetc(f))!=EOF)
		g_printf("%c",ch);
fclose(f);
g_printf("\n");
}

g_dir_close(dir);
return 0;
}
