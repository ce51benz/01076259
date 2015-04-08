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
gchar key[8];
guint32 size;
guint64 atime;
guint32 sblock;
gchar valid;
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
getput_param *p = (getput_param*)pa;

if(ready==0)err = EBUSY;
    else if(strlen(p->key)!=8)
	err = ENAMETOOLONG;
    else
    	err = 0;

rfos_complete_get(p->obj,p->inv,err);
g_free(p->path);
g_free(p->key);
g_free(p);
pthread_exit(0);

}

//worker function for put cmd
void *do_handle_put(void *pa){
guint err;
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
			NODE *tail =NULL,*head = NULL;
			guint32 i,blockloc,j,blockwrct=0,lastblock = 0,nextblock;
			for(i=0;i<8;i++)
				fe.key[i] = p->key[i];
			struct stat s;
			stat(p->path,&s);
			fe.size = s.st_size;
			//Delayed fe.atime
			//Check for avail. block
			j = ((vblock.bitvec_bl+vblock.file_ent_bl+1)/8)*8;
			for(i = (vblock.bitvec_bl+vblock.file_ent_bl+1)/8;;i++){
				if(j>=vblock.numblock)break; //0
 				if((bitvec[i] & 0x80) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+0;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+0;			
					}
					lastblock = (i*8)+0;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x80;
				}
				j++;
				if(j>=vblock.numblock)break; //1
				if((bitvec[i] & 0x40) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+1;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+1;			
					}
					lastblock = (i*8)+1;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x40;				
				}
				j++;
				if(j>=vblock.numblock)break; //2
				if((bitvec[i] & 0x20) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+2;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+2;			
					}
					lastblock = (i*8)+2;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x20;
				}
				j++;
				if(j>=vblock.numblock)break; //3
				if((bitvec[i] & 0x10) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+3;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+3;			
					}
					lastblock = (i*8)+3;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x10;
				}
				j++;
				if(j>=vblock.numblock)break; //4
				if((bitvec[i] & 0x08) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+4;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+4;			
					}
					lastblock = (i*8)+4;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x08;
				}
				j++;
				if(j>=vblock.numblock)break; //5
				if((bitvec[i] & 0x04) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+5;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+5;			
					}
					lastblock = (i*8)+5;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x04;
				}
				j++;
				if(j>=vblock.numblock)break; //6
				if((bitvec[i] & 0x02) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+6;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+6;			
					}
					lastblock = (i*8)+6;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x02;
				}
				j++;
				if(j>=vblock.numblock)break; //7
				if((bitvec[i] & 0x01) == 0){
					if(head == NULL){
						head = tail = g_new(NODE,1);
						head->data = g_new(DBLOCK,1);
						(head->data)->next = lastblock;
						fread((head->data)->data,1,28,fp);
						head->next = NULL;
						lastblock = blockloc = (i*8)+7;
					}
					else{
						NODE *ptr = g_new(NODE,1);
						ptr->data = g_new(DBLOCK,1);
						(ptr->data)->next = lastblock;
						fread((ptr->data)->data,1,28,fp);
						ptr->next = NULL;
						tail->next = ptr;
						tail = ptr;
						lastblock = blockloc = (i*8)+7;			
					}
					lastblock = (i*8)+7;
					blockwrct++;
					bitvec[i] = bitvec[i] | 0x01;
				}
				j++;
			}
			//CONTINUED
			NODE *ptr = head;nextblock = fe.sblock = blockloc;
			FILE * disk01 = fopen64(rfosdisk.disk1,"rb+");
			//WRITE DATA TO IMG DISK
			while(ptr != NULL){
				fseeko64(disk01,nextblock*32,SEEK_SET);
				fwrite(ptr->data,32,1,disk01);
				ptr = ptr->next;
			}
			//WRITE FREE BIT VEC TO IMG DISK
			fseeko64(disk01,32,SEEK_SET);
			fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk01);
			fe.valid = 1;
			fe.atime = time(NULL);
			//FILE LOCATION FOR AVAIL FILE ENT BLOCK AND WRITE VCB BACK
			if(!vblock.inv_file){
				vblock.numfile = vblock.numfile+1;
				vblock.free = vblock.free - blockwrct;
				fseeko64(disk01,((vblock.bitvec_bl+1)*32)+(vblock.numfile*sizeof(FILE_ENT)),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk01);
				fseeko64(disk01,0,SEEK_SET);
				fwrite(&vblock,32,1,disk01);
			}
			else{
				/* TO EDIT */
			}
			fclose(disk01);
			/*write entry FILE_ENT_INMEM to HTB*/
		}
		else
		{ /*TO EDIT CASE HTB NOT FOUND*/ }
    		err = 0;
		/*DO GARBAGE COLLECTION*/
		}
	}

rfos_complete_put(p->obj,p->inv,err);
g_free(p->path);
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
    rfos_complete_stat(object,invocation,0,0,0);
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
guint i,j=0;
guint32 startblock = vblock.bitvec_bl+vblock.file_ent_bl+1;
for(i = 0;;i++){
	
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x80;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x40;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x20;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x10;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x08;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x04;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x02;
	j++;
	if(j>=startblock)break;
	bitvec[i] = bitvec[i] | 0x01;
	j++;
}
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
guint32 i,j=0,bituse=0;
for(i = 0;;i++){
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x80) == 0x80)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x40) == 0x40)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x20) == 0x20)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x10) == 0x10)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x08) == 0x08)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x04) == 0x04)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x02) == 0x02)bituse++;
	j++;
	if(j>=vblock.numblock)break;
		if((bitvec[i] & 0x01) == 0x01)bituse++;
	j++;
}
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
	entry->size = fe.size;
	entry->atime = fe.atime;
	entry->sblock = fe.sblock;
	entry->valid = fe.valid;
	entry->fileno = i;

	g_hash_table_insert(filetable,entry->key,entry);
}
}
fclose(disk01);
}

/*fseek(s,-1,SEEK_END);
gchar *arr1 = "bank";
gchar *arr2 = "benz";
fwrite(arr1,1,4,s);
fflush(s);
*/

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
