#include "rfos.h"
#include <glib/gprintf.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
int ready = 0;
pthread_mutex_t lock;
GHashTable *filetable;
gchar *bitvec;
int bytechk[8] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
typedef struct _vcb{
guint32 diskno;
guint32 numblock;
guint32 free;
guint32 blocksize;
guint32 bitvec_bl;
guint32 file_ent_bl;
guint32 numfile;
guint32 inv_file;
}VCB;

VCB vblock;
typedef struct _fe{
char key[9];
guint32 size;
guint64 atime;
guint32 sblock;
char valid;
}FILE_ENT;

typedef struct _fe1{
gchar *key;
guint32 size;
guint64 atime;
guint32 sblock;
gchar valid;
guint32 fileno;
}FILE_ENT_INMEM;

typedef struct _block{
guint32 next;
gchar data[28];
}DBLOCK;

typedef struct _node{
DBLOCK *data;
struct _node *next;	
}NODE;

typedef struct handle_getput_param{
RFOS *obj;
GDBusMethodInvocation *inv;
gchar *key;
gchar *path;
}getput_param;

typedef struct handle_key_param{
RFOS *obj;
GDBusMethodInvocation *inv;
gchar *key;
}key_param;

typedef struct disk_param{
gchar *disk1;
gchar *disk2;
gchar *disk3;
gchar *disk4;
gint numdisk;
}DISK;

DISK rfosdisk;
//worker function for get cmd
void *do_handle_get(void *pa){
guint err;
FILE_ENT_INMEM *entry;
guint32 nextblock;
getput_param *p = (getput_param*)pa;

    if(ready==0)err = EBUSY;
    else if(strlen(p->key)!=8)
	err = ENAMETOOLONG;
    else{
	if((entry = (FILE_ENT_INMEM*)g_hash_table_lookup (filetable,p->key))!=NULL){
		FILE *fp = fopen(p->path,"wb");
		FILE *disk01 = fopen(rfosdisk.disk1,"rb");
		DBLOCK data;
		nextblock = entry->sblock;
		guint bread = 1;
		while(TRUE){
			g_printf("WRITE!\n");
			fseeko64(disk01,nextblock*32,SEEK_SET);
			fread(&data,32,1,disk01);
			if(!data.next){
				if(entry->size % 28 != 0)
					fwrite(data.data,1,entry->size % 28,fp);
				else
					fwrite(data.data,1,28,fp);
				break;
			}
			else
				fwrite(data.data,1,28,fp);
			//Update atime and write it back in disk to proper location (check file seq no and edit it without any confilct)
			nextblock = data.next;
		}
		fclose(fp);fclose(disk01);
    		err = 0;
	}
	else err = ENOENT;
	}


rfos_complete_get(p->obj,p->inv,err);
/*DO GARBAGE COLLECTION*/
//g_free(entry);
g_free(p->path);
g_free(p->key);
g_free(p);
g_printf("END FUNCTION\n");
pthread_exit(0);

}

//worker function for put cmd
void *do_handle_put(void *pa){
guint err;
NODE *tail =NULL,*head = NULL;
guint32 i,blockloc,j,blockwrct=0,nextblock,k;
getput_param *p = (getput_param*)pa;

    if(ready==0)err = EBUSY;
    else if(strlen(p->key)!=8)
	err = ENAMETOOLONG;
    else{
	FILE *fp = fopen64(p->path,"r");
	if(!fp)err = ENOENT;
	else{
		if(g_hash_table_contains(filetable,p->key)==FALSE)
		{
			FILE_ENT fe;
			for(i=0;i<8;i++)
				fe.key[i] = p->key[i];
			struct stat s;
			stat(p->path,&s);
			fe.size = s.st_size;
			fe.key[8] = '\0';
			//Check for avail. block
			j = ((vblock.bitvec_bl+vblock.file_ent_bl+1)/8)*8;
			for(i = (vblock.bitvec_bl+vblock.file_ent_bl+1)/8;;i++){
				for(k=0;k<8;k++){
					if(j>=vblock.numblock || feof(fp))goto freeblkrwchkpt; //0
 					if((bitvec[i] & bytechk[k]) == 0){
						if(head == NULL){
							head = tail = g_new(NODE,1);
							head->data = g_new(DBLOCK,1);
							(head->data)->next = 0;
							fread((head->data)->data,1,28,fp);
							head->next = NULL;
							blockloc = (i*8)+k;
						}
						else{
							NODE *ptr = g_new(NODE,1);
							ptr->data = g_new(DBLOCK,1);
							(ptr->data)->next = 0;
							fread((ptr->data)->data,1,28,fp);
							ptr->next = NULL;
							(tail->data)->next = (i*8)+k;
							tail->next = ptr;
							tail = ptr;			
						}
					blockwrct++;
					bitvec[i] = bitvec[i] | bytechk[k];
					}
					j++;
				}
			}
			freeblkrwchkpt:
			//CONTINUED
			fclose(fp);
			nextblock = fe.sblock = blockloc;NODE *ptr = head;
			FILE * disk01 = fopen64(rfosdisk.disk1,"rb+");
			//WRITE DATA TO IMG DISK
			while(ptr != NULL){
				fseeko64(disk01,nextblock*32,SEEK_SET);
				fwrite(ptr->data,32,1,disk01);
				nextblock = (ptr->data)->next;
				ptr = ptr->next;
			}
			//WRITE FREE BIT VEC TO IMG DISK
			fseeko64(disk01,32,SEEK_SET);
			fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk01);
			fe.valid = 1;
			fe.atime = time(NULL);
			vblock.numfile = vblock.numfile+1;
			vblock.free = vblock.free - blockwrct;
			//FILE LOCATION FOR AVAIL FILE ENT BLOCK AND WRITE VCB BACK
			if(!vblock.inv_file){
				fseeko64(disk01,((vblock.bitvec_bl+1)*32)+((vblock.numfile-1)*sizeof(FILE_ENT)),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk01);
				fseeko64(disk01,0,SEEK_SET);
				fwrite(&vblock,32,1,disk01);
				/*write entry FILE_ENT_INMEM to HTB*/
				FILE_ENT_INMEM *entry = g_new(FILE_ENT_INMEM,1);
				entry->key = g_strdup(fe.key);
				entry->size = fe.size;
				entry->atime = fe.atime;
				entry->sblock = fe.sblock;
				entry->valid = fe.valid;
				entry->fileno = vblock.numfile-1;
				g_hash_table_insert(filetable,entry->key,entry);
			}
			else{
				/* TO EDIT */
				/*Iterate hash table and find inv file and replace it to proper location*/
				/*Do not edit hashtable*/
				GHashTableIter iter;
				gpointer key,value;
				FILE_ENT_INMEM *fentry;
				g_hash_table_iter_init(&iter,filetable);
				while(g_hash_table_iter_next(&iter,&key,&value)){
					fentry = (FILE_ENT_INMEM*)value;
					if(!fentry->valid){
						g_hash_table_remove(filetable,fentry->key);
						g_free(fentry->key);
						fentry->key = g_strdup(fe.key);
						fentry->size = fe.size;
						fentry->atime = fe.atime;
						fentry->sblock = fe.sblock;
						fentry->valid = 1;
						g_hash_table_insert(filetable,fentry->key,fentry);						
						break;
					}
				}
				vblock.inv_file = vblock.inv_file - 1;
				fseeko64(disk01,((vblock.bitvec_bl+1)*32)+(fentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk01);
				fseeko64(disk01,0,SEEK_SET);
				fwrite(&vblock,32,1,disk01);
			}
			g_printf("KEY => %s\n",fe.key);
			g_printf("SIZE => %d\n",fe.size);
			g_printf("ATIME => %ld\n",fe.atime);
			g_printf("SBLOCK => %d\n",fe.sblock);
			g_printf("VALID => %d\n",fe.valid);
			fclose(disk01);
		}
		else
		{ g_printf("KEY ALREADY HAVE!\n");/*TO EDIT CASE HTB FOUND*/ }
    		err = 0;
		}
	}

rfos_complete_put(p->obj,p->inv,err);
NODE *ptr = head,*temp;
/*DO GARBAGE COLLECTION*/
while(ptr != NULL){
	g_free(ptr->data);
	temp = ptr;
	ptr = ptr->next;
	g_free(temp);	
	}
g_free(p->path);
g_free(p->key);
g_free(p);
pthread_exit(0);
}

void * do_handle_stat(void *pa){
key_param *p = (key_param *)pa;
guint32 size = 0;
guint err;
guint64 atime = 0;
FILE_ENT_INMEM *fentry;
if(!ready)err = EBUSY;
else if(strlen(p->key) != 8)err = ENAMETOOLONG;
else if((fentry = g_hash_table_lookup(filetable,p->key)) != NULL){
	size = fentry->size;
	atime = fentry->atime;
	err = 0;
	}
else
	err = ENOENT;
rfos_complete_stat(p->obj,p->inv,size,atime,err);
//GARBAGE COLLECTION
g_free(p->key);
g_free(p);
pthread_exit(0);
}
//Receive request then create thread to do the desired job(before do job,temp any data first)
static gboolean on_handle_get (
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key,
    const gchar *outpath) {
	pthread_t worker;
	getput_param *p = g_new(getput_param,1);
	p->obj = object;
	p->inv = invocation;
	p->key = g_strdup(key);
	p->path = g_strdup(outpath);

    pthread_create(&worker,NULL,do_handle_get,p);

    return TRUE;
}

static gboolean on_handle_put (
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key,
    const gchar *src) {
    pthread_t worker;
	getput_param *p = g_new(getput_param,1);
	p->obj = object;
	p->inv = invocation;
	p->key = g_strdup(key);
	p->path = g_strdup(src);

    pthread_create(&worker,NULL,do_handle_put,p);
    return TRUE;
}

static gboolean on_handle_remove(
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key){
    FILE *test = fopen("sdasdasd","wb");
    char *str = "test";
    fwrite(str,1,4,test);
    fclose(test);
    rfos_complete_remove(object,invocation,0);
    return TRUE;
}

static gboolean on_handle_search(
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key,
    const gchar *outpath){
    rfos_complete_search(object,invocation,0);
    return TRUE;
}

static gboolean on_handle_stat(
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key){
    pthread_t worker;
	key_param *p = g_new(key_param,1);
	p->obj = object;
	p->inv = invocation;
	p->key = g_strdup(key);
    pthread_create(&worker,NULL,do_handle_stat,p);
    return TRUE;
}

static void on_name_acquired (GDBusConnection *connection,
    const gchar *name,
    gpointer user_data)
{
    /* Create a new RFOS service object */
    RFOS *skeleton = rfos_skeleton_new ();
    /* Bind method invocation signals with the appropriate function calls */
    g_signal_connect (skeleton, "handle-get", G_CALLBACK (on_handle_get), NULL);
    g_signal_connect (skeleton, "handle-put", G_CALLBACK (on_handle_put), NULL);
    g_signal_connect (skeleton, "handle-remove", G_CALLBACK (on_handle_remove), NULL);
    g_signal_connect (skeleton,"handle-search",G_CALLBACK(on_handle_search),NULL);
    g_signal_connect (skeleton,"handle-stat",G_CALLBACK(on_handle_stat),NULL);
    /* Export the RFOS service on the connection as /kmitl/ce/os/RFOS object  */
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
        connection,
        "/kmitl/ce/os/RFOS",
        NULL);
}

void *dummywork(void *t){
struct stat s;
FILE *disk01 = fopen64(rfosdisk.disk1,"rb+");
if(!disk01){
g_printf("Open disk failed.\n");
}
else
{
fread(&vblock,32,1,disk01);
if(vblock.numblock == 0){
g_printf("FILE SYSTEM NOT FOUND SERVICE WILL FORMAT...\n");
stat(rfosdisk.disk1,&s);
vblock.diskno = 0;
vblock.blocksize = 32;
vblock.numblock = s.st_size/vblock.blocksize;
vblock.bitvec_bl = vblock.numblock/8/vblock.blocksize;
vblock.file_ent_bl = vblock.numblock*0.05;
vblock.free = vblock.numblock-vblock.bitvec_bl-vblock.file_ent_bl-1;
vblock.numfile = 0;
vblock.inv_file = 0;
bitvec = g_new(gchar,vblock.bitvec_bl*vblock.blocksize);
guint i,j=0,k;
guint32 startblock = vblock.bitvec_bl+vblock.file_ent_bl+1;
for(i = 0;;i++){
	for(k=0;k<8;k++){
		if(j>=startblock)goto filefmtchkpt;
		bitvec[i] = bitvec[i] | bytechk[k];
		j++;
	}
}
filefmtchkpt:
fseeko64(disk01,0,SEEK_SET);
fwrite(&vblock,32,1,disk01);
fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk01);
g_printf("FORMAT COMPLETE!\n");
}
else{
g_printf("FILE SYSTEM DETAIL....\n");
g_printf("DISK NO:%d\n",vblock.diskno);
g_printf("BLOCK SIZE:%d\n",vblock.blocksize);
g_printf("NUMBLOCK:%d\n",vblock.numblock);
g_printf("BIT VECTOR BLOCK COUNT:%d\n",vblock.bitvec_bl);
g_printf("FILE ENTRY BLOCK COUNT:%d\n",vblock.file_ent_bl);
g_printf("FREE:%d\n",vblock.free);
g_printf("FILE COUNT:%d\n",vblock.numfile);
g_printf("INVALID FILE:%d\n",vblock.inv_file);
bitvec = g_new(gchar,vblock.bitvec_bl*vblock.blocksize);
fread(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk01);
guint32 i,j=0,bituse=0,k;
for(i = 0;;i++){
	for(k=0;k<8;k++){
		if(j>=vblock.numblock)goto filerdrchkpt;
		if((bitvec[i] & bytechk[k]) == bytechk[k])bituse++;
	j++;
	}
}
filerdrchkpt:
g_printf("NUM OF BLOCK USED:%d\n",bituse);
FILE_ENT fe;
FILE_ENT_INMEM *entry;
g_printf("READ FILES:%d\n",vblock.numfile);
guint feoff = vblock.bitvec_bl+1;
fseeko64(disk01,feoff*32,SEEK_SET);
for(i=0;i<vblock.numfile;i++){
	fread(&fe,sizeof(FILE_ENT),1,disk01);
	entry = g_new(FILE_ENT_INMEM,1);
	entry->key = g_strdup(fe.key);
	g_printf("KEY => %s\n",entry->key);
	entry->size = fe.size;
	g_printf("SIZE => %d\n",entry->size);
	entry->atime = fe.atime;
	entry->sblock = fe.sblock;
	entry->valid = fe.valid;
	g_printf("ATIME => %ld\n",entry->atime);
	g_printf("SBLOCK => %d\n",entry->sblock);
	g_printf("VALID => %d\n",entry->valid);
	entry->fileno = i;

	g_hash_table_insert(filetable,entry->key,entry);
}
}
fclose(disk01);
}

ready = 1;
pthread_exit(0);
}
int main (int argc,char **argv)
{
pthread_t test;
if(argc > 1){
rfosdisk.numdisk = argc - 1;
	if(argc == 2){
	rfosdisk.disk1 = argv[1];
	}
	else if(argc == 3){
	rfosdisk.disk1 = argv[1];
	rfosdisk.disk2 = argv[2];
	}
	else if(argc == 4){
	rfosdisk.disk1 = argv[1];
	rfosdisk.disk2 = argv[2];
	rfosdisk.disk3 = argv[3];
	}
	else{
	rfosdisk.disk1 = argv[1];
	rfosdisk.disk2 = argv[2];
	rfosdisk.disk3 = argv[3];
	rfosdisk.disk4 = argv[4];
	}
	pthread_mutex_init(&lock,NULL);
    /* Initialize daemon main loop */
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    /* Attempt to register the daemon with 'kmitl.ce.os.RFOS' name on the bus */
    g_bus_own_name (G_BUS_TYPE_SESSION,
        "kmitl.ce.os.RFOS",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        NULL,
        on_name_acquired,
        NULL,
        NULL,
        NULL);
	filetable = g_hash_table_new(g_str_hash,g_str_equal);
    /* Start the main loop */

//Initialized RFOS (FORMATTING) via another thread
pthread_create(&test,NULL,dummywork,NULL);

    g_main_loop_run (loop);
}
else{
g_printf("Usage:./rfos-svc [disk1-name] [disk2-name] [disk3-name] [disk4-name]\n"); 
} 
   return 0;
}
