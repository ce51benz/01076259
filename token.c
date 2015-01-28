#include<glib.h>
#include<stdio.h>
struct word_node{
	gchar *word;
	gchar *doc_id;
};

struct keyval{
	GSList *head;
};

//Compare function use for sorting
gint cmp_word_node(const struct word_node *a,const struct word_node *b){
	int logicchk;
	logicchk = g_strcmp0(a->word,b->word);
	if(logicchk > 0)return 1;
	else if(logicchk < 0) return -1;
	else return atol(a->doc_id) > atol(b->doc_id);	
}


int main(int argc,char **argv){
FILE *f;
gchar ch;
gchar *filename,*watch;
gchar **number;
GString *temp;
gint j=0,ind;
struct word_node node;
GArray *wordlist = g_array_new(TRUE,FALSE,sizeof(struct word_node));
GDir *dir = g_dir_open(argv[1],0,NULL);
g_chdir(argv[1]);
temp = g_string_new(NULL);
while((filename = GINT_TO_POINTER(g_dir_read_name(dir))) !=NULL){
	f = fopen(filename,"r");
		while((ch = fgetc(f))!=EOF){
			if(g_ascii_isalpha(ch) && j == 0){
				g_string_append_c(temp,ch);
				j++;
			}
			else if(g_ascii_isalpha(ch))
				g_string_append_c(temp,ch);
			else if(!g_ascii_isalpha(ch) && j > 0){
				temp = g_string_ascii_down(temp);
				node.word = g_strdup(temp->str);
				g_string_erase(temp,0,-1);
				number = g_strsplit_set(filename,".txt",-1);
				node.doc_id = g_strdup(number[0]+(sizeof(gchar)*4));
				g_array_append_val(wordlist,node);
				j=0;
				g_strfreev(number);
			}
			else j=0;
		}	
	if(j > 0){
		temp = g_string_ascii_down(temp);
		node.word = g_strdup(temp->str);
		number = g_strsplit_set(filename,".txt",-1);
		node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
		g_array_append_val(wordlist,node);
		j=0;
		g_strfreev(number);
}
	fclose(f);
}

/*
g_printf("Before sort:\n");
for(ind = 0;ind < (*wordlist).len;ind++){
node = g_array_index(wordlist,struct word_node,ind);
watch = node.word;
g_printf("%s\t%s\n",(*watch).str,node.doc_id);
}
*/
g_array_sort(wordlist,(GCompareFunc) cmp_word_node);

/*
g_printf("After sort:\n");
for(ind =0;ind < (*wordlist).len;ind++){
node = g_array_index(wordlist,struct word_node,ind);
watch = node.word;
g_printf("%s\t%s\n",watch,node.doc_id);
}
*/

GArray *hashlist = g_array_new(TRUE,FALSE,sizeof(guint));
struct keyval *val = NULL;
GHashTable *table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
for(ind = 0;ind < (*wordlist).len;ind++){
	node = g_array_index(wordlist,struct word_node,ind);
	watch = node.word;
	//If preferred node is not exists create node and .... 
	if(!g_hash_table_contains(table,watch)){
	g_array_append_val(hashlist,watch);
	val = g_new(struct keyval,1);
	(*val).head = NULL;
	(*val).head = g_slist_append((*val).head,node.doc_id);
	g_hash_table_insert(table,watch,val);	
	}
	else{
		//Update value only since it dynamically allocated
		warp:
		if(g_slist_find_custom((*val).head,node.doc_id,(GCompareFunc)g_strcmp0)==NULL){
			(*val).head = g_slist_append((*val).head,node.doc_id);
		}
	    }
}
//Show table data
g_printf("Show data result:\n");

//Write data result to output
g_chdir("..");
f = fopen("outputtemp1","w");
g_fprintf(f,"%d\n",g_hash_table_size(table));
for(ind = 0;ind < (*hashlist).len;ind++){
	GSList *pt;
	watch = GUINT_TO_POINTER(g_array_index(hashlist,guint,ind));
	val = g_hash_table_lookup(table,watch);
	g_fprintf(f,"%s:%d:",watch,g_slist_length((*val).head));
	pt = (*val).head;
	while(pt != NULL){
		g_fprintf(f,"%s",(*pt).data);
		if((pt = g_slist_next(pt)) != NULL)
			g_fprintf(f,",");
	}
	g_fprintf(f,"\n");
}
fclose(f);




//Data deletion
for(ind = 0;ind < (*wordlist).len;ind++){
	node = g_array_index(wordlist,struct word_node,ind);
	watch = node.word;
	//g_string_free(watch,TRUE);
}
//Finally remove array
g_array_free(wordlist,TRUE);
g_hash_table_destroy(table);

//Clear key/data for each allocated
//Clear linklist

g_dir_close(dir);
return 0;
}

