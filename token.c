#include<malloc.h>
#include<stdlib.h>
#include<glib.h>
#include<stdio.h>
#include<pthread.h>
#include<string.h>
#include<sys/stat.h>
GHashTable *table;
GPtrArray *wordlist;

GDir *dir;
FILE *ff;
pthread_mutex_t tablelc;
pthread_mutex_t wordlistlc;
pthread_mutex_t dirlc;
pthread_mutex_t wordtemplc = PTHREAD_MUTEX_INITIALIZER;
gint exitstat;
GPtrArray *wcbox;

struct keyval{
	GPtrArray *arr;
};

struct wordcontainer{
	char *doc_id;
	GPtrArray *arrtemp;
};
//Compare function use for sorting
gint cmp_word_node(long *a,long *b){
	return g_strcmp0(GUINT_TO_POINTER(*a),GUINT_TO_POINTER(*b));
}

gint cmp_int(long* a,long* b){
	return atol(GUINT_TO_POINTER(*a)) - atol(GUINT_TO_POINTER(*b));
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
			g_ptr_array_sort(val->arr,(GCompareFunc)cmp_int);
		}
	}
else if(idx == 11){
	for(count = upper*11;count<wordlist->len;){
		watch = wordlist->pdata[count++];
		val = g_hash_table_lookup(table,watch);
		g_ptr_array_sort(val->arr,(GCompareFunc)cmp_int);
	}
}
else{
	GSList *ptr;int j;
	for(count = 0;count <wordlist->len;count++){		
		watch = wordlist->pdata[count]; 
		val = g_hash_table_lookup(table,watch);
		fprintf(ff,"%s:%d:",watch,val->arr->len);
		j=0;
		while(j < val->arr->len){
			fputs(val->arr->pdata[j++],ff);
			if(j < val->arr->len)
				fputc(',',ff);
		}
		fputc('\n',ff);
	}
}

}

void *tokenized_word(void *thnum){
	struct stat st;
	int i=0;
	long n;
	struct wordcontainer *wc;
	gchar* doc_id,*strwatch;
	char *filename,**number,*str;
	GHashTable *tb;
	tb = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
	GPtrArray *temparr;
	GString *temp = g_string_new(NULL);
	GPtrArray *tokentemp = g_ptr_array_new();
	FILE *f;
		while(1){
		pthread_mutex_lock(&dirlc);
		if((filename = GUINT_TO_POINTER(g_dir_read_name(dir))) == NULL){
		pthread_mutex_unlock(&dirlc);
		break;
		}
		f = fopen(filename,"rb");
		number = g_strsplit_set(filename,".txt",-1);			
		doc_id = g_strdup(number[0] + (sizeof(gchar)*4));
		pthread_mutex_unlock(&dirlc);
	
		stat(filename,&st);
		str = g_new(char,st.st_size);
		fread(str,sizeof(char),st.st_size,f);
		n = 0;		
		temparr = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
		while(n < st.st_size){
			if((str[n]>64&&str[n]<91)||(str[n]>96&&str[n]<123)){
				g_string_append_c(temp,str[n]);
			}
			else if(temp->len > 0){
				temp = g_string_ascii_down(temp);
				if(!g_hash_table_contains(tb,temp->str)){
					strwatch = g_strdup(temp->str);
					g_hash_table_add(tb,strwatch);
					g_ptr_array_add(temparr,strwatch);
				}
				g_string_erase(temp,0,-1);
			}
		n++;
		}
		g_free(str);
		wc = g_new(struct wordcontainer,1);
		wc->doc_id = doc_id;
		wc->arrtemp = temparr;
		g_hash_table_remove_all(tb);
		
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
int i=0,j;
struct keyval *val;
struct wordcontainer *wc;
	while(1){
		pthread_mutex_lock(&wordtemplc);
		if(exitstat > 2 && i >= wcbox->len){pthread_mutex_unlock(&wordtemplc);break;}
		while(i < wcbox->len){
			wc = wcbox->pdata[i++];
			pthread_mutex_unlock(&wordtemplc);	
			j=0;
			while(j<wc->arrtemp->len){
				if(!(val = g_hash_table_lookup(table,wc->arrtemp->pdata[j]))){
					val = g_new(struct keyval,1);
					val->arr = g_ptr_array_new();
					g_ptr_array_add(val->arr,wc->doc_id);
					g_hash_table_insert(table,g_strdup(wc->arrtemp->pdata[j]),val);
				}
				else if(!(val->arr->pdata[(val->arr->len)-1] == wc->doc_id)){
					g_ptr_array_add(val->arr,wc->doc_id);
				}
				j++;
			}
			g_ptr_array_free(wc->arrtemp,TRUE);
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
wordlist = g_ptr_array_new();
docopytable(NULL);
g_chdir("..");
ff = fopen("outputtemp1","wb");
g_fprintf(ff,"%d\n",wordlist->len);
pthread_create(&t1,&a1,sortlist,(void*)one);
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
