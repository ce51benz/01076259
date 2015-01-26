#include<glib.h>
#include<stdio.h>

struct word_node{
	GString *word;
	gchar *doc_id;
};

struct keyval{
	gchar *str;
	GSList *head;
	guint count;
};

gint cmp_word_node(const struct word_node *a,const struct word_node *b){
	int logicchk;
	GString *aa,*bb;
	aa = (*a).word;
	bb = (*b).word;
	logicchk = g_strcmp0((*aa).str,(*bb).str);
	if(logicchk > 0)return 1;
	else if(logicchk < 0) return -1;
	else return g_strcmp0((*a).doc_id,(*b).doc_id);	
}

void showtable(guint *hashkey,struct keyval *val,gint *etc){
	g_printf("%s:%d\n",(*val).str,(*val).count);
}

int main(int argc,char **argv){
FILE *f;
gchar ch;
gchar *filename;
gchar **number;
GString *temp,*watch;
gint j=0,ind;
struct word_node node;
GArray *wordlist = g_array_new(TRUE,FALSE,sizeof(struct word_node));
GDir *dir = g_dir_open(argv[1],0,NULL);
g_chdir(argv[1]);

while((filename = GINT_TO_POINTER(g_dir_read_name(dir))) !=NULL){
f = fopen(filename,"r");
g_printf("[%s]\n",filename);
	while((ch = fgetc(f))!=EOF){
		printf("%c",ch);
		if(g_ascii_isalpha(ch) && j == 0){
			temp = g_string_new(NULL);
			g_string_append_c(temp,ch);
			j++;	
		}
		else if(g_ascii_isalpha(ch))
			g_string_append_c(temp,ch);
		else if(!g_ascii_isalpha(ch) && j > 0){
			temp = g_string_ascii_down(temp);
			node.word = temp;
			number = g_strsplit_set(filename,".txt",-1);
			node.doc_id = number[0]+(sizeof(gchar)*4);
			g_array_append_val(wordlist,node);
			j=0;		
		}
		else j=0;
	}	
if(j > 0){
	temp = g_string_ascii_down(temp);
	node.word = temp;
	number = g_strsplit_set(filename,".txt",-1);
	node.doc_id = number[0] + (sizeof(gchar)*4);
	g_array_append_val(wordlist,node);
	j=0;
}
fclose(f);
}

g_printf("Before sort:\n");
for(ind = 0;ind < (*wordlist).len;ind++){
node = g_array_index(wordlist,struct word_node,ind);
watch = node.word;
g_printf("%s\t%s\n",(*watch).str,node.doc_id);
}
g_array_sort(wordlist,(GCompareFunc) cmp_word_node);

g_printf("After sort:\n");
for(ind =0;ind < (*wordlist).len;ind++){
node = g_array_index(wordlist,struct word_node,ind);
watch = node.word;
g_printf("%s\t%s\n",(*watch).str,node.doc_id);
}

guint *hashkey;
struct keyval *val;
GHashTable *table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
for(ind = 0;ind < (*wordlist).len;ind++){
	node = g_array_index(wordlist,struct word_node,ind);
	watch = node.word;
	hashkey = g_new(guint,1);
	(*hashkey) = g_str_hash((*watch).str);
	if((val = g_hash_table_lookup(table,hashkey)) == NULL){
	val = g_new(struct keyval,1);
	(*val).str = (*watch).str;
	(*val).head = NULL;
	(*val).count = 1;
	g_hash_table_insert(table,hashkey,val);
	}
	else{
	//g_printf("DO THIS\n");
	((*val).count)++;
	g_hash_table_insert(table,hashkey,val);
	}
}
g_printf("%d\n",g_hash_table_size(table) );
//Show table data
g_printf("Show data result:\n");
g_hash_table_foreach(table,(GHFunc)showtable,NULL);










//Data deletion
for(ind = 0;ind < (*wordlist).len;ind++){
node = g_array_index(wordlist,struct word_node,ind);
watch = node.word;
g_string_free(watch,TRUE);
}
//Finally remove array
g_array_free(wordlist,TRUE);
g_hash_table_destroy(table);
g_dir_close(dir);
return 0;
}

