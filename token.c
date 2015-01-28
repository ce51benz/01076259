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
GSList *listpt = NULL;
FILE *f;
gchar ch;
gchar *filename,*watch;
gchar **number;
GString *temp;
struct keyval *val;
GHashTable *table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
gint j=0,ind;
struct word_node node;
GArray *wordlist = g_array_new(TRUE,FALSE,sizeof(guint));
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
				number = g_strsplit_set(filename,".txt",-1);
				if(!(val = g_hash_table_lookup(table,temp->str))){
					val = g_new(struct keyval,1);
					node.word = g_strdup(temp->str);
					g_array_append_val(wordlist,node.word);
					node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
					val->head = g_slist_append(listpt,node.doc_id);
					g_hash_table_insert(table,node.word,val); 
				}
				else{
					if(!g_slist_find_custom(val->head,(number[0]+(sizeof(gchar)*4)),(GCompareFunc)g_strcmp0)){
						node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
						val->head = g_slist_append(val->head,node.doc_id);
					}
				}

				g_string_erase(temp,0,-1);
				j=0;
				g_strfreev(number);
			}
			else j=0;
		}	

	if(j > 0){
				temp = g_string_ascii_down(temp);
				number = g_strsplit_set(filename,".txt",-1);
				if(!(val = g_hash_table_lookup(table,temp->str))){
					val = g_new(struct keyval,1);
					node.word = g_strdup(temp->str);
					g_array_append_val(wordlist,node.word);
					node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
					val->head = g_slist_append(listpt,node.doc_id);
					g_hash_table_insert(table,node.word,val); 

				}
				else{
					if(!g_slist_find_custom(val->head,(number[0]+(sizeof(gchar)*4)),(GCompareFunc)g_strcmp0)){
						node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
						val->head = g_slist_append(val->head,node.doc_id);
					}
				}
		j=0;
		g_strfreev(number);
}
	fclose(f);
}

g_array_sort(wordlist,(GCompareFunc) g_strcmp0);

/*
g_printf("After sort:\n");
for(ind =0;ind < (*wordlist).len;ind++){
node = g_array_index(wordlist,struct word_node,ind);
watch = node.word;
g_printf("%s\t%s\n",watch,node.doc_id);
}
*/


//Show table data
g_printf("Show data result:\n");

//Write data result to output
g_chdir("..");
f = fopen("outputtemp1","w");
g_fprintf(f,"%d\n",g_hash_table_size(table));
for(ind = 0;ind < (*wordlist).len;ind++){
	GSList *pt;
	watch = GUINT_TO_POINTER(g_array_index(wordlist,guint,ind));
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


/*
//Data deletion
for(ind = 0;ind < (*wordlist).len;ind++){
	node = g_array_index(wordlist,struct word_node,ind);
	watch = node.word;
	//g_string_free(watch,TRUE);
}*/
//Finally remove array
g_array_free(wordlist,TRUE);
g_hash_table_destroy(table);

//Clear key/data for each allocated
//Clear linklist

g_dir_close(dir);
return 0;
}

