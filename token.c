#include<malloc.h>
#include<stdlib.h>
#include<glib.h>
#include<stdio.h>
#include<pthread.h>
#include<string.h>
#include<sys/stat.h>
GHashTable *table;
GPtrArray *wordlist;
GPtrArray *temperer[12];
gchar *path;
GDir *dir;
FILE *ff;
pthread_mutex_t tablelc;
pthread_mutex_t wordlistlc;
pthread_mutex_t dirlc = PTHREAD_MUTEX_INITIALIZER;
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
long idx = (long)index;int j;
long count;
long upper = wordlist->len/12;
char *watch,str[1000];
GString *temper = g_string_new(NULL);
struct keyval *val;
if(idx != 12 && idx != 11){
	int uppersum = upper * (idx+1);
	for(count = (upper*idx);count < uppersum;){
			watch = wordlist->pdata[count++];
			val = g_hash_table_lookup(table,watch);
			if(val->arr->len!=1){
				g_ptr_array_sort(val->arr,(GCompareFunc)cmp_int);
				j=0;
		
				sprintf(str,"%s:%d:",watch,val->arr->len);
				g_string_append(temper,str);
				while(j < val->arr->len){
					g_string_append(temper,val->arr->pdata[j++]);
				if(j < val->arr->len)
					g_string_append_c(temper,',');					
				}
				g_string_append_c(temper,'\n');
			}
			else{
			sprintf(str,"%s:%d:%s\n",watch,val->arr->len,val->arr->pdata[0]);		
			g_string_append(temper,str);			
			}
			g_ptr_array_add(temperer[idx],g_strdup(temper->str));
			g_string_erase(temper,0,-1);							
	}
}
else if(idx == 11){
	for(count = upper*11;count<wordlist->len;){
		watch = wordlist->pdata[count++];
		val = g_hash_table_lookup(table,watch);
		if(val->arr->len!=1){
		g_ptr_array_sort(val->arr,(GCompareFunc)cmp_int);
		j=0;
		sprintf(str,"%s:%d:",watch,val->arr->len);
			g_string_append(temper,str);
			while(j < val->arr->len){
				g_string_append(temper,val->arr->pdata[j++]);
			if(j < val->arr->len)
				g_string_append_c(temper,',');
			}
			g_string_append_c(temper,'\n');
		}
		else
			{
			sprintf(str,"%s:%d:%s\n",watch,val->arr->len,val->arr->pdata[0]);		
			g_string_append(temper,str);
			}
		
			g_ptr_array_add(temperer[idx],g_strdup(temper->str));
			g_string_erase(temper,0,-1);
	}
}
else{
	j=0;int k;
	while(j<=11){
		k=0;
		while(k<temperer[j]->len){
			fputs(temperer[j]->pdata[k++],ff);	
		}
		j++;
	}
    }

}

void *tokenized_word(void *thnum){
	struct stat st;
	char *fullpath,*slash = "/";
	int gcharsize = sizeof(gchar)*4;
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
			if(strlen(filename) < 4){pthread_mutex_unlock(&dirlc);continue;}
			fullpath = g_strconcat(path,slash,filename,NULL);
			f = fopen(fullpath,"rb");
			number = g_strsplit_set(filename,".txt",-1);			
			doc_id = g_strdup(number[0] + gcharsize);
			pthread_mutex_unlock(&dirlc);
			stat(fullpath,&st);
			g_free(fullpath);
			str = g_new(char,st.st_size);
			fread(str,sizeof(char),st.st_size,f);
			n = 0;		
			temparr = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
			while(n < st.st_size){
				if((str[n]>64&&str[n]<91)||(str[n]>96&&str[n]<123)){
					g_string_append_c(temp,str[n]);
				}
				else if(temp->len > 0){
					g_string_ascii_down(temp);
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
					val->arr = g_ptr_array_sized_new(1);
					g_ptr_array_add(val->arr,wc->doc_id);
					g_hash_table_insert(table,wc->arrtemp->pdata[j],val);
				}
				else {
					g_free(wc->arrtemp->pdata[j]);						
					if(!(val->arr->pdata[(val->arr->len)-1] == wc->doc_id)){
						g_ptr_array_add(val->arr,wc->doc_id);
					}
				}
				j++;
			}
			g_ptr_array_free(wc->arrtemp,FALSE);
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
int iterind;
long threadno[13] = {0,1,2,3,4,5,6,7,8,9,10,11,12};
pthread_mutex_init(&tablelc,NULL);
pthread_mutex_init(&wordlistlc,NULL);
mallopt(-8,2);

for(iterind=0;iterind <12;iterind++)
temperer[iterind] = g_ptr_array_new();

exitstat=0;
table = g_hash_table_new((GHashFunc)g_str_hash,(GEqualFunc)g_str_equal);
dir = g_dir_open(argv[1],0,NULL);
path = argv[1];
pthread_t thread[11];
pthread_attr_t attr[11];

for(iterind=0;iterind<11;iterind++){
pthread_attr_init(&attr[iterind]);
pthread_attr_setstacksize(&attr[iterind],1024000);
}
wcbox = g_ptr_array_new();
pthread_create(&thread[0],&attr[0],tokenized_word,NULL);
pthread_create(&thread[1],&attr[1],tokenized_word,NULL);
pthread_create(&thread[2],&attr[2],tokenized_word,NULL);
//Do parallel task
wordtotable(NULL);
pthread_join(thread[0],NULL);
pthread_join(thread[1],NULL);
pthread_join(thread[2],NULL);
g_dir_close(dir);
wordlist = g_ptr_array_new();
docopytable(NULL);
ff = fopen("output","wb");
g_fprintf(ff,"%d\n",wordlist->len);

for(iterind=0;iterind <11;iterind++)
pthread_create(&thread[iterind],&attr[iterind],sortlist,(void*)threadno[iterind+1]);

sortlist((void*)threadno[0]);

for(iterind=0;iterind <11;iterind++)
pthread_join(thread[iterind],NULL);

sortlist((void*)threadno[12]);
fclose(ff);

//Finally remove array
g_ptr_array_free(wordlist,TRUE);
g_hash_table_destroy(table);
return 0;
}
