#include<glib.h>
#include<stdio.h>
#include<pthread.h>
//#include<stdlib.h>
GHashTable *table;
GArray *wordlist;

GDir *dir;
GSList *listpt;
FILE *ff;
pthread_mutex_t tablelc;
pthread_mutex_t wordlistlc;
pthread_mutex_t dirlc;

struct word_node{
	gchar *word;
	gchar *doc_id;
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
long upper = wordlist->len/4;
gchar *watch;
if(idx != 0 && idx != 3){
	int uppersum = upper * (idx+1);
	GSList **head;
	for(count = upper*idx;count < uppersum;count++){
			watch = GUINT_TO_POINTER(g_array_index(wordlist,long,count)); 
			head = g_hash_table_lookup(table,watch);
			head[0] = g_slist_sort(head[0],(GCompareFunc)cmp_int);
		}
	}
else if(idx == 3){
	GSList **head;
	GSList *ptr;
	for(count = upper*3;count<wordlist->len;count++){
		watch = GUINT_TO_POINTER(g_array_index(wordlist,long,count));
		head = g_hash_table_lookup(table,watch);
		head[0] = g_slist_sort(head[0],(GCompareFunc)cmp_int);
	}
}
else{
	GSList *ptr;
	GSList **head;
	for(count = 0;count < upper;count++){
			watch = GUINT_TO_POINTER(g_array_index(wordlist,long,count)); 
			head = g_hash_table_lookup(table,watch);
			g_fprintf(ff,"%s:%d:",watch,g_slist_length(head[0]));
			ptr = g_slist_sort(head[0],(GCompareFunc)cmp_int);
			while(ptr !=NULL){
				g_fprintf(ff,"%s",ptr->data);
					if((ptr = g_slist_next(ptr)) != NULL)
						g_fprintf(ff,",");
			}
			g_fprintf(ff,"\n");
	}

	
	for(count = upper;count <wordlist->len;count++){		
		watch = GUINT_TO_POINTER(g_array_index(wordlist,long,count)); 
		head = g_hash_table_lookup(table,watch);
		g_fprintf(ff,"%s:%d:",watch,g_slist_length(head[0]));
		ptr = head[0];
		while(ptr != NULL){
			g_fprintf(ff,"%s",ptr->data);
			if((ptr = g_slist_next(ptr)) != NULL)
				g_fprintf(ff,",");
		}
		g_fprintf(ff,"\n");}
}

}

void *tokenized_word(void *thnum){
long threadnum = (long)thnum;int n;
struct word_node node;
char ch;
gchar *filename,*watchtemp;
gchar **number;
GPtrArray *locktemp = NULL;
GString *temp = g_string_new(NULL);
locktemp = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
GSList **head;
FILE *f;
	while(1){
		pthread_mutex_lock(&dirlc);
		if((filename = GUINT_TO_POINTER(g_dir_read_name(dir))) == NULL){
		pthread_mutex_unlock(&dirlc);
		break;
		}
		pthread_mutex_unlock(&dirlc);
		f = fopen(filename,"r");
		number = g_strsplit_set(filename,".txt",-1);	
		node.doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
		while((ch = fgetc(f))!=EOF || locktemp->len != 0){
		if((ch>64&&ch<91)||(ch>96&&ch<123)){
			g_string_append_c(temp,ch);
		}
		else {
			temp = g_string_ascii_down(temp);
			if(pthread_mutex_trylock(&tablelc) != 0)goto locktemppt;
			if(locktemp->len != 0){
				for(n = 0;n<locktemp->len;n++){
					watchtemp = g_ptr_array_index(locktemp,n);
					if(!(head = g_hash_table_lookup(table,watchtemp))){
						head = g_new(GSList*,4);
						head[0] = head[1] = head[2] = head[3] =NULL; 
						head[threadnum]=g_slist_prepend(listpt,node.doc_id);
						node.word = g_strdup(watchtemp);
						g_array_append_val(wordlist,node.word);
						g_hash_table_insert(table,node.word,head);
					}
					else{
						if(!head[threadnum]){
							head[threadnum] = g_slist_prepend(NULL,node.doc_id);
						}
						else if(!(head[threadnum]->data == node.doc_id)){
							head[threadnum] = g_slist_prepend(head[threadnum],node.doc_id);
						}
					    }
			}//END LOOP TEMP			 
			g_ptr_array_remove_range(locktemp,0,locktemp->len);
			}//END IF
				
				if(temp->len > 0){
					if(!(head = g_hash_table_lookup(table,temp->str))){
						head = g_new(GSList*,4);
						head[0] = head[1] = head[2] = head[3] = NULL;						
						head[threadnum] = g_slist_prepend(listpt,node.doc_id);
						node.word = g_strdup(temp->str);
						g_array_append_val(wordlist,node.word);
						g_hash_table_insert(table,node.word,head);
					}
					else{
						if(!head[threadnum]){
							head[threadnum] = g_slist_prepend(NULL,node.doc_id);
						}
						else if(!(head[threadnum]->data == node.doc_id)){
							head[threadnum] = g_slist_prepend(head[threadnum],node.doc_id);
						}
				 	   }
				}	
			pthread_mutex_unlock(&tablelc);
			g_string_erase(temp,0,-1);
		}
		continue;
		locktemppt:
		
		if(temp->len >0){
		node.word = g_strdup(temp->str);
		g_ptr_array_add(locktemp,node.word);
		g_string_erase(temp,0,-1);
		}
	}
	fclose(f);
	g_strfreev(number);
	}
}



void concatlist(gchar *key,GSList **head,gpointer usrdata){
	head[0] = g_slist_concat(head[0],head[1]);
	head[2] = g_slist_concat(head[2],head[3]);	
	head[0] = g_slist_concat(head[0],head[2]);
}

void *doconcatlist(void *data){
g_hash_table_foreach(table,(GHFunc)concatlist,NULL);
}

int main(int argc,char **argv){
long oh=0,one=1,two=2,three=3;
listpt = NULL;
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
tokenized_word((void*)three);
pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);

pthread_create(&t1,&a1,doconcatlist,NULL);
g_array_sort(wordlist,(GCompareFunc) cmp_word_node);
pthread_join(t1,NULL);

g_chdir("..");
ff = fopen("outputtemp1","w");
g_fprintf(ff,"%d\n",wordlist->len);


pthread_create(&t1,&a1,sortlist,(void*)oh);
pthread_create(&t2,&a2,sortlist,(void*)one);
pthread_create(&t3,&a3,sortlist,(void*)two);
sortlist((void*)three);

pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);
//Show table data

fclose(ff);

//Finally remove array
g_array_free(wordlist,TRUE);
g_hash_table_destroy(table);

//Clear key/data for each allocated
//Clear linklist

g_dir_close(dir);
return 0;
}
