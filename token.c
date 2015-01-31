#include<glib.h>
#include<stdio.h>
#include<pthread.h>

GHashTable *table;
GArray *wordlist;

GDir *dir;

pthread_mutex_t tablelc;
pthread_mutex_t wordlistlc;
pthread_mutex_t dirlc;

struct word_node{
	gchar *word;
	gchar *doc_id;
};

struct keyval{
	GSList *head;
	gchar *thr[3];
};

//Compare function use for sorting
gint cmp_word_node(long *a,long *b){
	return g_strcmp0(GUINT_TO_POINTER(*a),GUINT_TO_POINTER(*b));
}

gint cmp_int(gpointer a,gpointer b){
	return atol(a) - atol(b);
}

void *sortlist(void *index){
long idx = (long)index;
long count;
for(count = ((wordlist->len)/4)*idx;count < ((wordlist->len)/4)*(idx+1);count++){
gchar *watch = GUINT_TO_POINTER(g_array_index(wordlist,long,count)); 
struct keyval *val2 = g_hash_table_lookup(table,watch);
val2->head = g_slist_sort(val2->head,(GCompareFunc)cmp_int);
}

}
void *tokenized_word(void *thnum){
gboolean deleteflag;
long threadnum = (long)thnum;
GSList *listpt = NULL;
struct word_node node;
gchar ch;
gchar *filename; 
gchar **number; 
struct keyval *newval,*val; 
GString *temp = g_string_new(NULL);
FILE *f;
	while(1){
		pthread_mutex_lock(&dirlc);
		if((filename = GUINT_TO_POINTER(g_dir_read_name(dir))) == NULL){
		pthread_mutex_unlock(&dirlc);
		pthread_exit(0);
		}
		pthread_mutex_unlock(&dirlc);
		f = fopen(filename,"r");
		number = g_strsplit_set(filename,".txt",-1);	
		node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
	
		while((ch = fgetc(f))!=EOF){
		if(g_ascii_isalpha(ch)){
			g_string_append_c(temp,ch);
		}
		else if(temp->len >0){
			temp = g_string_ascii_down(temp);
			node.word = g_strdup(temp->str);
			newval = g_new(struct keyval,1);
			newval->thr[threadnum] = node.doc_id;
			newval->head = g_slist_prepend(listpt,node.doc_id);
			pthread_mutex_lock(&tablelc);
			if(!(val = g_hash_table_lookup(table,temp->str))){
				//if((val = g_hash_table_lookup(table,temp->str))!=NULL){pthread_mutex_unlock(&tablelc);goto warp;}
				g_array_append_val(wordlist,node.word);
				g_hash_table_insert(table,node.word,newval);
				pthread_mutex_unlock(&tablelc);
				}
			else{
				pthread_mutex_unlock(&tablelc);
				g_slist_free(newval->head);
				g_free(node.word);
				g_free(newval);
				pthread_mutex_lock(&wordlistlc);
				if(!(val->thr[threadnum] == node.doc_id)){
					val->thr[threadnum] = node.doc_id;
					val->head = g_slist_prepend(val->head,node.doc_id);
				}
			      	pthread_mutex_unlock(&wordlistlc);
			}
			/*if(val!=NULL){
			g_slist_free(newval->head);
			g_free(node.word);
			g_free(newval);
			}*/
			g_string_erase(temp,0,-1);
		}
	
	}
	fclose(f);
	g_strfreev(number);
	}
pthread_exit(0);
}

int main(int argc,char **argv){
long oh=0,one=1,two=2,three=3;
FILE *ff;
pthread_mutex_init(&tablelc,NULL);
pthread_mutex_init(&wordlistlc,NULL);
pthread_mutex_init(&dirlc,NULL);
gint ind;
gchar *watch;
table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
wordlist = g_array_new(TRUE,FALSE,sizeof(long));
dir = g_dir_open(argv[1],0,NULL);
g_chdir(argv[1]);
pthread_t t1;
pthread_t t2;
pthread_t t3;
pthread_attr_t a1;
pthread_attr_t a2;
pthread_attr_t a3;
pthread_attr_init(&a1);
pthread_attr_init(&a2);
pthread_attr_init(&a3);
pthread_create(&t1,&a1,tokenized_word,(void*)oh);
pthread_create(&t2,&a2,tokenized_word,(void*)one);
pthread_create(&t3,&a3,tokenized_word,(void*)two);
//Do parallel task

pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);


g_array_sort(wordlist,(GCompareFunc) cmp_word_node);
pthread_create(&t1,&a1,sortlist,(void*)oh);
pthread_create(&t2,&a2,sortlist,(void*)one);
pthread_create(&t3,&a3,sortlist,(void*)two);
sortlist((void*)three);

pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);
//Show table data

GSList *pt;
struct keyval *val1;
//Write data result to output
g_chdir("..");
ff = fopen("outputtemp1","w");
g_fprintf(ff,"%d\n",wordlist->len);
for(ind = 0;ind < wordlist->len;ind++){
	watch = GUINT_TO_POINTER(g_array_index(wordlist,long,ind));
	val1 = g_hash_table_lookup(table,watch);
	g_fprintf(ff,"%s:%d:",watch,g_slist_length(val1->head));
	pt = val1->head;
	while(pt != NULL){
		g_fprintf(ff,"%s",pt->data);
		if((pt = g_slist_next(pt)) != NULL)
			g_fprintf(ff,",");
	}
	g_fprintf(ff,"\n");
}
fclose(ff);

//Finally remove array
g_array_free(wordlist,TRUE);
g_hash_table_destroy(table);

//Clear key/data for each allocated
//Clear linklist

g_dir_close(dir);
return 0;
}

