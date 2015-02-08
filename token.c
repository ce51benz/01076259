#include<glib.h>
#include<stdio.h>
#include<pthread.h>
GHashTable *table;
GPtrArray *wordlist;

GDir *dir;
FILE *ff;
pthread_mutex_t tablelc;
pthread_mutex_t wordlistlc;
pthread_mutex_t dirlc;
pthread_mutex_t wordtemplc;
gint exitstat;
GPtrArray *wcbox;

struct keyval{
	GSList *head;
};

struct wordcontainer{
	gchar *doc_id;
	GHashTable *tbl;
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
struct keyval *val;
if(idx != 0 && idx != 3){
	int uppersum = upper * (idx+1);
	for(count = (upper*idx);count < uppersum;count++){
			watch = wordlist->pdata[count];
			val = g_hash_table_lookup(table,watch);
			val->head = g_slist_sort(val->head,(GCompareFunc)cmp_int);
		}
	}
else if(idx == 3){
	for(count = upper*3;count<wordlist->len;count++){
		watch = wordlist->pdata[count];
		val = g_hash_table_lookup(table,watch);
		val->head = g_slist_sort(val->head,(GCompareFunc)cmp_int);
	}
}
else{
	GSList *ptr;
	for(count = 0;count < upper;count++){
			watch = wordlist->pdata[count]; 
			val = g_hash_table_lookup(table,watch);
			fprintf(ff,"%s:%d:",watch,g_slist_length(val->head));
			ptr = g_slist_sort(val->head,(GCompareFunc)cmp_int);
			while(ptr !=NULL){
				fputs(ptr->data,ff);
					if((ptr = ptr->next) != NULL)
						fputc(',',ff);
			}
			fputc('\n',ff);
	}
	
	for(count = upper;count <wordlist->len;count++){		
		watch = wordlist->pdata[count]; 
		val = g_hash_table_lookup(table,watch);
		fprintf(ff,"%s:%d:",watch,g_slist_length(val->head));
		ptr = val->head;
		while(ptr != NULL){
			fputs(ptr->data,ff);
			if((ptr = ptr->next) != NULL)
				fputc(',',ff);
		}
		fputc('\n',ff);
	}
}

}

void *tokenized_word(void *thnum){
	char ch;
	struct wordcontainer *wc;
	gchar *doc_id,*filename;
	gchar **number;
	GHashTable *tb;
	GString *temp = g_string_new(NULL);
	FILE *f;
	while(1){
		pthread_mutex_lock(&dirlc);
		if((filename = GUINT_TO_POINTER(g_dir_read_name(dir))) == NULL){
			pthread_mutex_unlock(&dirlc);break;
		}
		pthread_mutex_unlock(&dirlc);
		f = fopen(filename,"r");
		number = g_strsplit_set(filename,".txt",-1);			
		doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
		tb = g_hash_table_new_full((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal,(GDestroyNotify)g_free,NULL);
		while((ch = fgetc(f)) != EOF){
			if((ch>64&&ch<91)||(ch>96&&ch<123)){
				g_string_append_c(temp,ch);
			}
			else if(temp->len > 0){ 
				temp = g_string_ascii_down(temp);
				if(!g_hash_table_contains(tb,temp->str))
				g_hash_table_add(tb,g_strdup(temp->str));
				g_string_erase(temp,0,-1);
			}
		}
		pthread_mutex_lock(&wordtemplc);
		wc = g_new(struct wordcontainer,1);
		wc->doc_id = doc_id;
		wc->tbl = tb;
		g_ptr_array_add(wcbox,wc);
		pthread_mutex_unlock(&wordtemplc);
		fclose(f);
		g_strfreev(number);
	}
	pthread_mutex_lock(&wordtemplc);
	exitstat++;
	pthread_mutex_unlock(&wordtemplc);
	//g_string_free(temp,TRUE);
}
void *wordtotable(void *thnum){
wcbox = g_ptr_array_new();
int i=0;
char ch;
struct keyval *val;
struct wordcontainer *wc;
gchar **number;
GHashTableIter *it;
gpointer watch;
GString *temp = g_string_new(NULL);
FILE *f;
	while(1){
		pthread_mutex_lock(&wordtemplc);
		if(exitstat > 2 && i >= wcbox->len){pthread_mutex_unlock(&wordtemplc);break;}
		while(i < wcbox->len){
			wc = wcbox->pdata[i++];
			g_hash_table_iter_init(it,wc->tbl);
			while(g_hash_table_iter_next(it,&watch,NULL)){
				if(!(val = g_hash_table_lookup(table,watch))){
					val = g_new(struct keyval,1);
					val->head = g_slist_prepend(NULL,wc->doc_id);
					g_hash_table_insert(table,watch,val);				
				}
				else if(!(val->head->data == wc->doc_id)){
					val->head = g_slist_prepend(val->head,wc->doc_id);
				}
			}
			//g_hash_table_remove_all(wc->tbl);
			//g_hash_table_destroy(wc->tbl);
		}
		pthread_mutex_unlock(&wordtemplc);	
	}
}


void *tabletoarray(gchar *key,gpointer head,gpointer usrdata){
	g_ptr_array_add(wordlist,key);
}

void *docopytable(void *data){
g_hash_table_foreach(table,(GHFunc)tabletoarray,NULL);
g_ptr_array_sort(wordlist,(GCompareFunc) cmp_word_node);

}
int main(int argc,char **argv){
long oh=0,one=1,two=2,three=3;
pthread_mutex_init(&tablelc,NULL);
pthread_mutex_init(&wordlistlc,NULL);
pthread_mutex_init(&dirlc,NULL);
pthread_mutex_init(&wordtemplc,NULL);
gint ind;
exitstat=0;
table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
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
wcbox = g_ptr_array_new();
pthread_create(&t1,&a1,tokenized_word,NULL);
pthread_create(&t2,&a2,tokenized_word,NULL);
pthread_create(&t3,&a3,tokenized_word,NULL);
//Do parallel task
wordtotable(NULL);
pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);
g_dir_close(dir);
wordlist = g_ptr_array_sized_new(g_hash_table_size(table));
docopytable(NULL);
pthread_create(&t1,&a1,sortlist,(void*)one);
g_chdir("..");
ff = fopen("outputtemp1","w");
g_fprintf(ff,"%d\n",wordlist->len);
pthread_create(&t2,&a2,sortlist,(void*)two);
pthread_create(&t3,&a3,sortlist,(void*)three);
sortlist((void*)oh);

pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);
//Show table data

fclose(ff);

//Finally remove array
g_ptr_array_free(wordlist,TRUE);
g_hash_table_destroy(table);
return 0;
}

