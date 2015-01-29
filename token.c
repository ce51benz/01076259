#include<glib.h>
#include<stdio.h>
struct word_node{
	gchar *word;
	gchar *doc_id;
};
struct keyval{
	GSList *head;
};
gint cmp_int(gpointer a,gpointer b){
return atol(a) - atol(b);
}

gboolean treetofile(gchar *key,struct keyval *val,FILE *f){
GSList *pt;
g_fprintf(f,"%s:%d:",key,g_slist_length(val->head));
pt = g_slist_sort(val->head,(GCompareFunc)cmp_int);
while(pt != NULL){
	g_fprintf(f,"%s",pt->data);
	pt = g_slist_next(pt);
	if(pt != NULL)g_fprintf(f,",");
}
g_fprintf(f,"\n");
return FALSE;
}

int main(int argc,char **argv){
GSList *listpt = NULL;
FILE *f;
gchar ch;
gchar *filename,*watch;
gchar **number;
GString *temp;
struct keyval *val;
GTree *tree = g_tree_new((GCompareFunc)g_strcmp0);
gint j=0,ind;
struct word_node node;
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
				if(!(val = g_tree_lookup(tree,temp->str))){
					val = g_new(struct keyval,1);
					node.word = g_strdup(temp->str);
					node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
					val->head = g_slist_append(listpt,node.doc_id);
					g_tree_insert(tree,node.word,val); 
				}
				else{
					if(!g_slist_find_custom(val->head,(number[0]+(sizeof(gchar)*4)),(GCompareFunc)g_strcmp0)){
						node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
						val->head = g_slist_prepend(val->head,node.doc_id);
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
				if(!(val = g_tree_lookup(tree,temp->str))){
					val = g_new(struct keyval,1);
					node.word = g_strdup(temp->str);
					node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
					val->head = g_slist_append(listpt,node.doc_id);
					g_tree_insert(tree,node.word,val); 

				}
				else{
					if(!g_slist_find_custom(val->head,(number[0]+(sizeof(gchar)*4)),(GCompareFunc)g_strcmp0)){
						node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
						val->head = g_slist_prepend(val->head,node.doc_id);
					}
				}
		j=0;
		g_strfreev(number);
}
	fclose(f);
}

//g_array_sort(wordlist,(GCompareFunc) cmp_word_node);

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
g_fprintf(f,"%d\n",g_tree_nnodes(tree));

g_tree_foreach(tree,(GTraverseFunc) treetofile,f);


/*
for(ind = 0;ind < (*wordlist).len;ind++){
	GSList *pt;
	watch = GUINT_TO_POINTER(g_array_index(wordlist,guint,ind));
	val = g_hash_table_lookup(table,watch);
	g_fprintf(f,"%s:%d:",watch,g_slist_length((*val).head));
	pt = g_slist_sort(val->head,(GCompareFunc)cmp_int);
	
	while(pt != NULL){
		g_fprintf(f,"%s",(*pt).data);
		if((pt = g_slist_next(pt)) != NULL)
			g_fprintf(f,",");
	}
	g_fprintf(f,"\n");
}*/


fclose(f);


/*
//Data deletion
for(ind = 0;ind < (*wordlist).len;ind++){
	node = g_array_index(wordlist,struct word_node,ind);
	watch = node.word;
	//g_string_free(watch,TRUE);
}*/

g_tree_destroy(tree);

//Clear key/data for each allocated
//Clear linklist

g_dir_close(dir);
return 0;
}

