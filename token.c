#include<malloc.h>
#include<stdlib.h>
#include<glib.h>
#include<stdio.h>
#include<pthread.h>
#include<string.h>
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
	char *doc_id;
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
long upper = wordlist->len/12;
char *watch;
struct keyval *val;
if(idx != 12 && idx != 11){
	int uppersum = upper * (idx+1);
	for(count = (upper*idx);count < uppersum;){
			watch = wordlist->pdata[count++];
			val = g_hash_table_lookup(table,watch);
			val->head = g_slist_sort(val->head,(GCompareFunc)cmp_int);
		}
	}
else if(idx == 11){
	for(count = upper*11;count<wordlist->len;){
		watch = wordlist->pdata[count++];
		val = g_hash_table_lookup(table,watch);
		val->head = g_slist_sort(val->head,(GCompareFunc)cmp_int);
	}
}
else{
	GSList *ptr;
	for(count = 0;count <wordlist->len;count++){		
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
	char ch;int i=0;
	struct wordcontainer *wc;
	char *doc_id,*filename;
	char **number;
	GHashTable *tb;
	GString *temp = g_string_new(NULL);
	GPtrArray *tokentemp = g_ptr_array_new();
	FILE *f;
		while((filename = GUINT_TO_POINTER(g_dir_read_name(dir))) != NULL){
		f = fopen(filename,"r");
		number = g_strsplit_set(filename,".txt",-1);			
		doc_id = strdup(number[0] + (sizeof(gchar)*4));
		tb = g_hash_table_new_full((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal,(GDestroyNotify)g_free,NULL);
		while((ch = fgetc(f)) != EOF){
			if((ch>64&&ch<91)||(ch>96&&ch<123)){
				g_string_append_c(temp,ch);
			}
			else if(temp->len > 0){
				temp = g_string_ascii_down(temp);
				if(!g_hash_table_contains(tb,temp->str))
				g_hash_table_add(tb,strdup(temp->str));
				g_string_erase(temp,0,-1);
			}
			
		}

		wc = g_new(struct wordcontainer,1);
		wc->doc_id = doc_id;
		wc->tbl = tb;
		if(pthread_mutex_trylock(&wordtemplc) !=0)goto checkpoint;
		for(;i< tokentemp->len;i++)
		g_ptr_array_add(wcbox,tokentemp->pdata[i]);
		g_ptr_array_add(wcbox,wc);
		fclose(f);
		g_strfreev(number);
		pthread_mutex_unlock(&wordtemplc);
		continue;
		checkpoint:
		g_ptr_array_add(tokentemp,wc);
		fclose(f);
		g_strfreev(number);
	}
	pthread_mutex_lock(&wordtemplc);
		for(;i< tokentemp->len;i++)	
		g_ptr_array_add(wcbox,tokentemp->pdata[i]);
	exitstat++;
	pthread_mutex_unlock(&wordtemplc);
	g_string_free(temp,TRUE);
}
void *wordtotable(void *thnum){
char ch;int i=0;
struct keyval *val;
struct wordcontainer *wc;
gchar **number,*filename,*doc_id;
GHashTableIter *it;
gpointer watch;
GString *temp1 = g_string_new(NULL);
FILE *f;/*
	while((filename = GUINT_TO_POINTER(g_dir_read_name(dir)))!=NULL){
		f = fopen(filename,"r");
		number = g_strsplit_set(filename,".txt",-1);			
		doc_id = strdup(number[0] + (sizeof(gchar)*4));
		while((ch = fgetc(f))!=EOF){
			if((ch>64&&ch<91)||(ch>96&&ch<123)){
				g_string_append_c(temp1,ch);
			}
			else if(temp1->len > 0){
				temp1 = g_string_ascii_down(temp1);
				if(!(val = g_hash_table_lookup(table,temp1->str))){			
					val = g_new(struct keyval,1);
					val->head = g_slist_prepend(NULL,doc_id);
					g_hash_table_insert(table,g_strdup(temp1->str),val);
				}
				else if(val->head->data == doc_id){
					val->head = g_slist_prepend(val->head,doc_id);
				}
				g_string_erase(temp1,0,-1);
			}
		}
		fclose(f);
		g_strfreev(number);
	}*/
	while(1){
		pthread_mutex_lock(&wordtemplc);
		if(exitstat > 2 && i >= wcbox->len){pthread_mutex_unlock(&wordtemplc);break;}
		while(i < wcbox->len){
			wc = wcbox->pdata[i++];
			pthread_mutex_unlock(&wordtemplc);	
			g_hash_table_iter_init(it,wc->tbl);
			while(g_hash_table_iter_next(it,&watch,NULL)){
				if(!(val = g_hash_table_lookup(table,watch))){
					val = g_new(struct keyval,1);
					val->head = g_slist_prepend(NULL,wc->doc_id);
					g_hash_table_insert(table,g_strdup(watch),val);
				}
				else if(!(val->head->data == wc->doc_id)){
					val->head = g_slist_prepend(val->head,wc->doc_id);
				}
			}
			g_hash_table_destroy(wc->tbl);
			pthread_mutex_lock(&wordtemplc);	
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
long twelve=12,oh=0,one=1,two=2,three=3,four=4,five=5,six=6,seven=7,eight=8,nine=9,ten=10,eleven=11;
pthread_mutex_init(&tablelc,NULL);
pthread_mutex_init(&wordlistlc,NULL);
pthread_mutex_init(&dirlc,NULL);
pthread_mutex_init(&wordtemplc,NULL);
mallopt(-8,1);
exitstat=0;
table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
dir = g_dir_open(argv[1],0,NULL);
g_chdir(argv[1]);
pthread_t t1;
pthread_t t2;
pthread_t t3;
pthread_t t4;
pthread_t t5;
pthread_t t6;
pthread_t t7;
pthread_t t8;
pthread_t t9;
pthread_t t10;
pthread_t t11;
pthread_attr_t a1;
pthread_attr_init(&a1);
pthread_attr_setstacksize(&a1,1024000);
wcbox = g_ptr_array_new();
pthread_create(&t1,&a1,tokenized_word,NULL);
pthread_create(&t2,&a1,tokenized_word,NULL);
pthread_create(&t3,&a1,tokenized_word,NULL);
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
pthread_create(&t2,&a1,sortlist,(void*)two);
pthread_create(&t3,&a1,sortlist,(void*)three);
pthread_create(&t4,&a1,sortlist,(void*)four);
pthread_create(&t5,&a1,sortlist,(void*)five);
pthread_create(&t6,&a1,sortlist,(void*)six);
pthread_create(&t7,&a1,sortlist,(void*)seven);
pthread_create(&t8,&a1,sortlist,(void*)eight);
pthread_create(&t9,&a1,sortlist,(void*)nine);
pthread_create(&t10,&a1,sortlist,(void*)ten);
pthread_create(&t11,&a1,sortlist,(void*)eleven);
sortlist((void*)oh);
pthread_join(t1,NULL);
pthread_join(t2,NULL);
pthread_join(t3,NULL);
pthread_join(t4,NULL);
pthread_join(t5,NULL);
pthread_join(t6,NULL);
pthread_join(t7,NULL);
pthread_join(t8,NULL);
pthread_join(t9,NULL);
pthread_join(t10,NULL);
pthread_join(t11,NULL);
sortlist((void*)twelve);
//Show table data
fclose(ff);

//Finally remove array
g_ptr_array_free(wordlist,TRUE);
g_hash_table_destroy(table);
return 0;
}
