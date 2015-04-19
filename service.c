#include "rfos.h"
#include <glib/gprintf.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include<malloc.h>
#include<glib.h>
#include<math.h>
int ready = 0;
int desiresize;
pthread_mutex_t lock;
GHashTable *filetable;
gchar *bitvec;
GArray *actoft,*rfosoft;
int bytechk[8] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
typedef struct _aofe{
guint64 fid;
char mode;
}AOF_ENT;

typedef struct _rfosofe{
char key[9];
char mode;
}RFOSOFT_ENT;
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
gchar data[1020];
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
gchar **disk;
gint numdisk;
}DISK;

DISK rfosdisk;

gboolean isOpenForRead(guint64 fid){
int i;AOF_ENT ent;
for(i=0;i<actoft->len;i++){
	ent = g_array_index(actoft,AOF_ENT,i);
	if(ent.fid==fid&&ent.mode=='R'){return TRUE;}
}
return FALSE;
}

gboolean rfosIsOpenForRead(char *key){
int i;RFOSOFT_ENT ent;
for(i=0;i<rfosoft->len;i++){
	ent = g_array_index(rfosoft,RFOSOFT_ENT,i);
	if(!g_strcmp0(ent.key,key)&&ent.mode=='R')return TRUE;
}
return FALSE;
}


void remOpenForRead(guint64 fid){
int i;AOF_ENT ent;
for(i=0;i<actoft->len;i++){
	ent = g_array_index(actoft,AOF_ENT,i);
	if(ent.fid==fid&&ent.mode=='R')
		{g_array_remove_index(actoft,i);break;}
}
}

void rfosRemOpenForRead(char *key){
int i;RFOSOFT_ENT ent;
for(i=0;i<rfosoft->len;i++){
	ent = g_array_index(rfosoft,RFOSOFT_ENT,i);
	if(!g_strcmp0(ent.key,key)&&ent.mode=='R'){
		{g_array_remove_index(rfosoft,i);break;}
	}
}
}

gboolean isOpenForWrite(guint64 fid){
int i;AOF_ENT ent;
for(i=0;i<actoft->len;i++){
	ent = g_array_index(actoft,AOF_ENT,i);
	if(ent.fid==fid&&ent.mode=='W')return TRUE;
}
return FALSE;
}

gboolean rfosIsOpenForWrite(char *key){
int i;RFOSOFT_ENT ent;
for(i=0;i<rfosoft->len;i++){
	ent = g_array_index(rfosoft,RFOSOFT_ENT,i);
	if(!g_strcmp0(ent.key,key)&&ent.mode=='W')return TRUE;
}
return FALSE;
}


void remOpenForWrite(guint64 fid){
int i;AOF_ENT ent;
for(i=0;i<actoft->len;i++){
	ent = g_array_index(actoft,AOF_ENT,i);
	if(ent.fid==fid&&ent.mode=='W')
		{g_array_remove_index(actoft,i);break;}
}
}

void rfosRemOpenForWrite(char *key){
int i;RFOSOFT_ENT ent;
for(i=0;i<rfosoft->len;i++){
	ent = g_array_index(rfosoft,RFOSOFT_ENT,i);
	if(!g_strcmp0(ent.key,key)&&ent.mode=='W'){
		{g_array_remove_index(rfosoft,i);break;}
	}
}
}


int strcmpcus(long *str1,long *str2){
return g_strcmp0(GUINT_TO_POINTER(*str1),GINT_TO_POINTER(*str2));
}

gboolean isAlnumStr(char *str){
int l = strlen(str),n;
for(n=0;n<l;n++){
	if(!isalnum(str[n]))return FALSE;
}
return TRUE;
}
void *do_handle_search(void *pa){
guint err;
FILE_ENT_INMEM *fentry;
getput_param *p = (getput_param*)pa;
gpointer key,value;
GHashTableIter iter;
GPtrArray *arr = NULL;
if(!ready)err = EBUSY;
else if(strlen(p->key)>8 || (!isAlnumStr(p->key)))err =ENAMETOOLONG;
else{
	arr = g_ptr_array_new();
	g_hash_table_iter_init(&iter,filetable); //hash table must not be modified!?????
	while(g_hash_table_iter_next(&iter,&key,&value)){
		fentry = (FILE_ENT_INMEM *)value;
		if(fentry->valid){
			if(!strncmp(fentry->key,p->key,strlen(p->key)))
				g_ptr_array_add(arr,fentry->key);	
		}
	}
	if(arr->len){
		//Create directory for output file
		FILE *fp;		
		g_ptr_array_sort(arr,(GCompareFunc)strcmpcus);
		struct stat64 fst;
		//-------lock--------------
		if(!stat64(p->path,&fst)){
			if(isOpenForRead(fst.st_ino) || isOpenForWrite(fst.st_ino)){
				err = EAGAIN;goto searchchkpt;
			}
			else{
			fp = fopen64(p->path,"wb");
			if(!fp){
				if(g_mkdir_with_parents(p->path,0777)){
					err = ENOENT;goto searchchkpt;
				}
				rmdir(p->path);
				fp = fopen64(p->path,"wb");
			}
			stat64(p->path,&fst);
			AOF_ENT ent;
			ent.fid = fst.st_ino;
			ent.mode = 'W';
			g_array_append_val(actoft,ent);
			}
		}
		else{
			fp = fopen64(p->path,"wb");
			if(!fp){
				if(g_mkdir_with_parents(p->path,0777)){
					err = ENOENT;goto searchchkpt;
				}
				rmdir(p->path);
				fp = fopen64(p->path,"wb");
			}
			stat64(p->path,&fst);
			AOF_ENT ent;
			ent.fid = fst.st_ino;
			ent.mode = 'W';
			g_array_append_val(actoft,ent);
		}
		//-------------------------
		guint32 i = 0;
		while(i<arr->len){
			fprintf(fp,"%s",(char*)arr->pdata[i]);
			i++;
			if(i<arr->len)fputc(',',fp);
			if(ferror(fp)){err=errno;fclose(fp);remOpenForWrite(fst.st_ino);goto searchchkpt;}
		}
		fclose(fp);remOpenForWrite(fst.st_ino);err=0;
	}
	else err = ENOENT;
}
searchchkpt:
rfos_complete_search(p->obj,p->inv,err);
//DO GARBAGE COLLECTION
if(arr != NULL)
g_ptr_array_free(arr,TRUE);
g_free(p->key);
g_free(p->path);
pthread_exit(0);
}
//worker function for get cmd
void *do_handle_get(void *pa){
guint err;int i,primary;gboolean isBoth = FALSE;
FILE_ENT_INMEM *entry;
guint32 nextblock;
FILE *disk[rfosdisk.numdisk];
for(i =0;i<rfosdisk.numdisk;i++)
	disk[i] = NULL;
getput_param *p = (getput_param*)pa;

    if(!ready)err = EBUSY;
    else if(strlen(p->key)!=8 || (!isAlnumStr(p->key)))
	err = ENAMETOOLONG;
    else{
	if((entry = (FILE_ENT_INMEM*)g_hash_table_lookup (filetable,p->key))!=NULL){ //also check for valid
		if(!entry->valid)err = ENOENT;
		else{
			//Create directory for output file....
			//GFile *files = g_file_new_for_path(p->path);
			//-------lock1--------------
			if(rfosIsOpenForWrite(p->key)){
				err = EAGAIN;goto getchkpt;
			}
			else{
				RFOSOFT_ENT rent;
				for(i = 0;i<8;i++)
					rent.key[i] = p->key[i];
				rent.key[8] = '\0';
				rent.mode = 'R';
 				g_array_append_val(rfosoft,rent);
			}
			//--------------------------
			FILE *fp;
			struct stat64 fst;
			//-------lock2--------------
			if(!stat64(p->path,&fst)){
				if(isOpenForRead(fst.st_ino) || isOpenForWrite(fst.st_ino)){
					err = EAGAIN;goto getchkpt;
				}
				else{	
					fp = fopen64(p->path,"wb");
					if(!fp){
						if(g_mkdir_with_parents(p->path,0777)){
							err = ENOENT;rfosRemOpenForRead(p->key);goto getchkpt;
					}
					rmdir(p->path);
					fp = fopen64(p->path,"wb");
					}				
					stat64(p->path,&fst);
					AOF_ENT ent;
					ent.fid = fst.st_ino;
					ent.mode = 'W';
					g_array_append_val(actoft,ent);
				}
			}
			else{
				fp = fopen64(p->path,"wb");
				if(!fp){
					if(g_mkdir_with_parents(p->path,0777)){
						err = ENOENT;rfosRemOpenForRead(p->key);goto getchkpt;
					}
					rmdir(p->path);
					fp = fopen64(p->path,"wb");
				}				
				stat64(p->path,&fst);
				AOF_ENT ent;
				ent.fid = fst.st_ino;
				ent.mode = 'W';
				g_array_append_val(actoft,ent);
			}
			//-------------------------

			//SELECT PRIMARY DISK TO READ FROM
			//********************************
			for(i=0;i<rfosdisk.numdisk;i++)
			disk[i] = fopen64(rfosdisk.disk[i],"rb+");
			//********************************	
			if(disk[0]&&disk[1]){
				primary = 0;isBoth = TRUE;
			}
			else if(disk[0]&&!disk[1])
				primary = 0;
			else
				primary = 1;
			DBLOCK cur;
			cur.next = entry->sblock;
			nextblock = 0;
			//----------------------------
			while(TRUE){
				if((nextblock+1)-cur.next) 
					fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
				nextblock = cur.next;
				fread(&cur,vblock.blocksize,1,disk[primary]);
				if(!cur.next){
					if(entry->size % desiresize != 0){
						fwrite(cur.data,1,entry->size % desiresize,fp);
						if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth)
								for(i = 0;i<rfosdisk.numdisk;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary]);
						remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
					}
					else{
						fwrite(cur.data,1,desiresize,fp);
					if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth)
								for(i = 0;i<rfosdisk.numdisk;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary]);
						remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
					    }
					break;
				}
				else{
					fwrite(cur.data,1,desiresize,fp);
					if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth)
								for(i = 0;i<rfosdisk.numdisk;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary]);
					remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
				    }
				//Update atime and write it back in disk to proper location (check file seq no and edit it without any confilct)
			}

			//-----------------------------
			entry->atime = time(NULL);
			FILE_ENT fe;
			fe.size = entry->size;
			int i;
			for(i =0;i<8;i++)
			fe.key[i] = entry->key[i];
			fe.key[8] = '\0';
			fe.atime = entry->atime;
			fe.sblock = entry->sblock;
			fe.valid = entry->valid;
			//------------------------		
			
			//determine number of disk to write update!
			if(isBoth){
				for(i=0;i<rfosdisk.numdisk;i++){	
		        	fseeko64(disk[i],(((vblock.bitvec_bl+1)*vblock.blocksize)+(entry->fileno*sizeof(FILE_ENT))),SEEK_SET);			
				fwrite(&fe,sizeof(FILE_ENT),1,disk[i]);
				fclose(disk[i]);
				}
			}
			else    {
				fseeko64(disk[primary],(((vblock.bitvec_bl+1)*vblock.blocksize)+(entry->fileno*sizeof(FILE_ENT))),SEEK_SET);			
				fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
				fclose(disk[primary]);
				}
			//------------------------    
			fclose(fp);remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);	
			err = 0;
		}
	}
	else err = ENOENT;
	}

getchkpt:
rfos_complete_get(p->obj,p->inv,err);
/*DO GARBAGE COLLECTION*/
g_free(p->path);
g_free(p->key);
g_free(p);
pthread_exit(0);

}

//worker function for put cmd
void *do_handle_put(void *pa){
guint err;
gboolean isFirst = TRUE;
GArray *dataarr = g_array_new(TRUE,FALSE,sizeof(DBLOCK));
DBLOCK cur;
guint32 i,blockloc,j,blockwrct=0,nextblock,k,primary,n;
gboolean isBoth = FALSE;
FILE_ENT_INMEM *hfentry;FILE_ENT fe;
FILE *disk[rfosdisk.numdisk];
for(i=0;i<rfosdisk.numdisk;i++)
	disk[i] = NULL;
getput_param *p = (getput_param*)pa;

    if(!ready)err = EBUSY;
    else if(strlen(p->key)!=8 || (!isAlnumStr(p->key)))
	err = ENAMETOOLONG;
    else{
	struct stat64 fst;
	//-----------------lock-----------------
	if(stat64(p->path,&fst))err = ENOENT;
	else{
		if(isOpenForWrite(fst.st_ino)){err = EAGAIN;goto putchkpt;}
		AOF_ENT ent;
		ent.fid = fst.st_ino;
		ent.mode = 'R';
		g_array_append_val(actoft,ent);
		//--------------------------------------
		FILE *fp = fopen64(p->path,"r");
				
			if((hfentry = g_hash_table_lookup(filetable,p->key)) == NULL)
			{
				//----------------lockforkey--------------
				if(rfosIsOpenForRead(p->key)||rfosIsOpenForWrite(p->key)){
					err = EAGAIN;fclose(fp);remOpenForRead(fst.st_ino);goto putchkpt;
				}
				else{
					RFOSOFT_ENT rent;
					for(i = 0;i<8;i++)
						rent.key[i] = p->key[i];
					rent.key[8] = '\0';
					rent.mode = 'W';
					g_array_append_val(rfosoft,rent);
				}
				//----------------------------------------
				for(i=0;i<8;i++)
					fe.key[i] = p->key[i];
				fe.size = fst.st_size;
				fe.key[8] = '\0';
				//Check for avail. block
				if(ceil(fe.size*1.0 / (vblock.blocksize-4)) > vblock.free){
					fclose(fp);
					err = ENOSPC;
					remOpenForRead(fst.st_ino);
					rfosRemOpenForWrite(p->key);
					goto putchkpt;
				}
				j = ((vblock.bitvec_bl+vblock.file_ent_bl+1)/8)*8;
				for(i = (vblock.bitvec_bl+vblock.file_ent_bl+1)/8;;i++){
					for(k=0;k<8;k++){
						if(j>=vblock.numblock || feof(fp))goto freeblkrwchkpt; //0
 						if((bitvec[i] & bytechk[k]) == 0){
							if(isFirst){
								cur.next = 0;
								fread(cur.data,1,desiresize,fp);
								blockloc = (i*8)+k;
								isFirst = FALSE;
							}
							else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr,cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;			
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
				remOpenForRead(fst.st_ino);
				g_array_append_val(dataarr,cur);
				cur.next = fe.sblock = blockloc;
				nextblock = 0;

				//-----------------------------------------------
				for(n = 0;n<rfosdisk.numdisk;n++)
					disk[n] = fopen64(rfosdisk.disk[n],"rb+");
				//-----------------------------------------------
				
				if(disk[0]&&disk[1]){
					primary = 0;isBoth = TRUE;
				}
				else if(disk[0]&&!disk[1])
					primary = 0;
				else
					primary = 1;
				/////-------------------1------------------------------
				if(isBoth){
					for(n = 0;n<rfosdisk.numdisk;n++){	
						cur.next = fe.sblock = blockloc;
						nextblock = 0;					
						for(i = 0;i<dataarr->len;i++){
							if((nextblock+1)-cur.next){				
								fseeko64(disk[n],cur.next*vblock.blocksize,SEEK_SET);
							}
							nextblock = cur.next;
							cur = g_array_index(dataarr,DBLOCK,i);
							fwrite(&cur,vblock.blocksize,1,disk[n]);
						}
					}
				}
				else{
					for(i = 0;i<dataarr->len;i++){
						if((nextblock+1)-cur.next){				
							fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
						}
						nextblock = cur.next;
						cur = g_array_index(dataarr,DBLOCK,i);
						fwrite(&cur,vblock.blocksize,1,disk[primary]);
					}
				}
					
				/////-------------------1------------------------------
				
				//WRITE FREE BIT VEC TO IMG DISK

				/////-------------------2------------------------------
				if(isBoth){
					for(n = 0;n<rfosdisk.numdisk;n++){
						fseeko64(disk[n],vblock.blocksize,SEEK_SET);
						fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[n]);
					}
				}
				else{
					fseeko64(disk[primary],vblock.blocksize,SEEK_SET);
					fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[primary]);
				}
				/////-------------------2------------------------------
				fe.valid = 1;
				fe.atime = time(NULL);
				vblock.free = vblock.free - blockwrct;
				//FILE LOCATION FOR AVAIL FILE ENT BLOCK AND WRITE VCB BACK
				if(!vblock.inv_file){
					vblock.numfile = vblock.numfile+1;
					/////-------------------3------------------------------
					if(isBoth){
						for(n = 0;n<rfosdisk.numdisk;n++){
							fseeko64(disk[n],((vblock.bitvec_bl+1)*vblock.blocksize)+((vblock.numfile-1)*sizeof(FILE_ENT)),SEEK_SET);
							fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
							fseeko64(disk[n],0,SEEK_SET);
							fwrite(&vblock,vblock.blocksize,1,disk[n]);
						}
					}
					else{
						fseeko64(disk[primary],((vblock.bitvec_bl+1)*vblock.blocksize)+((vblock.numfile-1)*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
						fseeko64(disk[primary],0,SEEK_SET);
						fwrite(&vblock,vblock.blocksize,1,disk[primary]);
						}
					/////-------------------3------------------------------
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

					/////-------------------4------------------------------
					if(isBoth){		
						for(n = 0;n<rfosdisk.numdisk;n++){
						fseeko64(disk[n],((vblock.bitvec_bl+1)*vblock.blocksize)+(fentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
						fseeko64(disk[n],0,SEEK_SET);
						fwrite(&vblock,vblock.blocksize,1,disk[n]);
						}
					}
					else{
						fseeko64(disk[primary],((vblock.bitvec_bl+1)*vblock.blocksize)+(fentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
						fseeko64(disk[primary],0,SEEK_SET);
						fwrite(&vblock,vblock.blocksize,1,disk[primary]);
					}
					/////-------------------4------------------------------
				}
				g_printf("NORMAL CASE\n");
				g_printf("KEY => %s\n",fe.key);
				g_printf("SIZE => %d\n",fe.size);
				g_printf("ATIME => %ld\n",fe.atime);
				g_printf("SBLOCK => %d\n",fe.sblock);
				g_printf("VALID => %d\n",fe.valid);
				
				/////-------------------5------------------------------
				if(isBoth){
				for(n = 0;n<rfosdisk.numdisk;n++)
					fclose(disk[n]);
				}
				else fclose(disk[primary]);
				/////-------------------5------------------------------
				rfosRemOpenForWrite(p->key);
			}
		else
			{ 
				/*TO EDIT CASE HTB FOUND*/
				if(hfentry->valid){
				//Track what block is already allocated
				//Check that whether number of block which already allocated is less than required
				//If yes clear the rest of unused block as unallocated
				//Else find way to allocate more
					for(i=0;i<8;i++)
						fe.key[i] = p->key[i];
					fe.size = fst.st_size;
					fe.key[8] = '\0';
					//----------------lockforkey--------------
					if(rfosIsOpenForRead(p->key)||rfosIsOpenForWrite(p->key)){
						err = EAGAIN;fclose(fp);remOpenForRead(fst.st_ino);goto putchkpt;
					}
					else{
						RFOSOFT_ENT rent;
						for(i = 0;i<8;i++)
							rent.key[i] = p->key[i];
						rent.key[8] = '\0';
						rent.mode = 'W';
 						g_array_append_val(rfosoft,rent);
					}
					//----------------------------------------
					//Check for avail block.
					if(ceil(hfentry->size*1.0/(vblock.blocksize-4)) == ceil(fe.size*1.0/(vblock.blocksize-4))){
						//this case can replace data instantly!
						//no need to update vblock
						
						//---------------9------------------------------
						for(n = 0;n<rfosdisk.numdisk;n++)
							disk[n] = fopen64(rfosdisk.disk[n],"rb+");
						//---------------9------------------------------
						if(disk[0]&&disk[1]){
							primary = 0;isBoth = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary = 0;
						else
							primary = 1;
						
						DBLOCK cur;
						nextblock =0;
						cur.next = hfentry->sblock;
						//------------------10 readonly-----------------
						do{
						if((nextblock+1)-cur.next)
							fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock.blocksize,1,disk[primary]);
							fread(cur.data,1,desiresize,fp);
							g_array_append_val(dataarr,cur);
						}while(cur.next);
						//------------------10 readonly-----------------

						nextblock =0;
						cur.next = hfentry->sblock;
						//-------------------------11-------------------------
						if(isBoth){
							for(n=0;n<rfosdisk.numdisk;n++){
								nextblock =0;
								cur.next = hfentry->sblock;
								for(i = 0;i<dataarr->len;i++){
									if((nextblock+1)-cur.next)
										fseeko64(disk[n],cur.next*vblock.blocksize,SEEK_SET);
									nextblock = cur.next;
									cur = g_array_index(dataarr,DBLOCK,i);
									fwrite(&cur,vblock.blocksize,1,disk[n]);
								}
							}
						}
						else{
							for(i = 0;i<dataarr->len;i++){
								if((nextblock+1)-cur.next)
									fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
								nextblock = cur.next;
								cur = g_array_index(dataarr,DBLOCK,i);
								fwrite(&cur,vblock.blocksize,1,disk[primary]);
							}
						}
						//-------------------------11-------------------------
						//Update fe block..... and write back!
						fe.atime = time(NULL);
						fe.valid = 1;
						fe.sblock = hfentry->sblock;
					 	hfentry->size = fe.size;
						hfentry->atime = fe.atime;
					//-----------------------12-------------------------------
					if(isBoth){			
					for(n=0;n<rfosdisk.numdisk;n++){	
	fseeko64(disk[n],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);					
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);					
						fclose(disk[n]);
						}
					}
					else{
	fseeko64(disk[primary],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);					
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);					
						fclose(disk[primary]);
					}
					//-----------------------12-------------------------------
						rfosRemOpenForWrite(p->key);
						g_printf("Case htb found with eq block size req\n");	
						g_printf("KEY => %s\n",fe.key);
						g_printf("SIZE => %d\n",fe.size);
						g_printf("ATIME => %ld\n",fe.atime);
						g_printf("SBLOCK => %d\n",fe.sblock);
						g_printf("VALID => %d\n",fe.valid);	
					}
					else if(ceil(hfentry->size*1.0/(vblock.blocksize-4)) < ceil(fe.size*1.0/(vblock.blocksize-4))){
					//this case must allocate additional block via find free block...
					//Use similar procedure of normal case and do it!
					//Update block for decreased free value,bitvec and fe block
						if((ceil(fe.size*1.0 / (vblock.blocksize-4))-ceil(hfentry->size*1.0/(vblock.blocksize-4))) > vblock.free){
							fclose(fp);err = ENOSPC;
							remOpenForRead(fst.st_ino);rfosRemOpenForWrite(p->key);goto putchkpt;			
						}
						//------------------thirtheen-----------------
						for(n = 0;n<rfosdisk.numdisk;n++)
							disk[n] = fopen(rfosdisk.disk[n],"rb+");
						//------------------thirtheen-----------------
						if(disk[0]&&disk[1]){
							primary = 0;isBoth = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary = 0;
						else
							primary = 1;
						
						cur.next = hfentry->sblock;
						nextblock = 0;
						//-----------------14 readonly----------------
						do{
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock.blocksize,1,disk[primary]);
							fread(cur.data,1,desiresize,fp);
							if(cur.next)
								g_array_append_val(dataarr,cur);
						}while(cur.next);
						//-----------------14 readonly----------------
						
						//The last block has invalid block no.(has no.0)
						j = ((vblock.bitvec_bl+vblock.file_ent_bl+1)/8)*8;
						for(i = (vblock.bitvec_bl+vblock.file_ent_bl+1)/8;;i++){
							for(k=0;k<8;k++){
								if(j>=vblock.numblock || feof(fp))goto freeblkrwchkpt3; //0
 								if((bitvec[i] & bytechk[k]) == 0){	
									cur.next = (i*8)+k;
									g_array_append_val(dataarr,cur);
									cur.next = 0;
									fread(cur.data,1,desiresize,fp);		
									blockwrct++;
									bitvec[i] = bitvec[i] | bytechk[k];
								}
							j++;
							}
						}			
						freeblkrwchkpt3:
						g_array_append_val(dataarr,cur);
						//Write main data to disk
						cur.next = hfentry->sblock;
						nextblock = 0;
						
						//----------------------15--------------------
						if(isBoth){
							for(n=0;n<rfosdisk.numdisk;n++){
								cur.next = hfentry->sblock;
								nextblock = 0;
								for(i = 0;i<dataarr->len;i++){
									if((nextblock+1)-cur.next){				
										fseeko64(disk[n],cur.next*vblock.blocksize,SEEK_SET);
									}
									nextblock = cur.next;
									cur = g_array_index(dataarr,DBLOCK,i);
									fwrite(&cur,vblock.blocksize,1,disk[n]);
								}
							}
						}
						else{
							for(i = 0;i<dataarr->len;i++){
								if((nextblock+1)-cur.next){				
									fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
								}
								nextblock = cur.next;
								cur = g_array_index(dataarr,DBLOCK,i);
								fwrite(&cur,vblock.blocksize,1,disk[primary]);
							}
						}
						//----------------------15--------------------
						fe.atime = time(NULL);
						fe.sblock = hfentry->sblock;
						fe.valid = 1;
						
						vblock.free = vblock.free - blockwrct;
						
						//--------------------16--------------------
						if(isBoth){
							for(n = 0;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);				
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
						fseeko64(disk[n],0,SEEK_SET);
					fwrite(&vblock,vblock.blocksize,1,disk[n]);								
						fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[n]);
						fclose(disk[n]);
						}
						}
						else{
						fseeko64(disk[primary],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);				
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
						fseeko64(disk[primary],0,SEEK_SET);
					fwrite(&vblock,vblock.blocksize,1,disk[primary]);								
						fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[primary]);
						fclose(disk[primary]);
						}
						//--------------------16--------------------
						rfosRemOpenForWrite(p->key);
						hfentry->size = fe.size;
						hfentry->atime = fe.atime;
						g_printf("Case htb found with more block size req\n");	
						g_printf("KEY => %s\n",fe.key);
						g_printf("SIZE => %d\n",fe.size);
						g_printf("ATIME => %ld\n",fe.atime);
						g_printf("SBLOCK => %d\n",fe.sblock);
						g_printf("VALID => %d\n",fe.valid);	
					}
					else{
						//this case must allocate block less than the old one
						//So if allocate until end of desired file
						//The rest of old file will freed
						//Update fe block and bitvec!
						//------ also update vblock to increase free
						//-----------------------17--------------------
						for(n=0;n<rfosdisk.numdisk;n++)
							disk[n] = fopen64(rfosdisk.disk[n],"rb+");
						//-----------------------17--------------------
						if(disk[0]&&disk[1]){
							primary = 0;isBoth = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary = 0;
						else
							primary = 1;
						
						DBLOCK cur;guint tempnext;
						cur.next = hfentry->sblock;
						nextblock = 0;

						//------------------18 readonly------------------------
						while(TRUE){
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock.blocksize,1,disk[primary]);
							fread(cur.data,1,desiresize,fp);
							
							if(!feof(fp)){
								g_array_append_val(dataarr,cur);
							}
							else{
								tempnext = cur.next;
								cur.next = 0;
								g_array_append_val(dataarr,cur);
								break;
							}
						}
						
						cur.next = tempnext;
						
						while(cur.next){
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock.blocksize,1,disk[primary]);
							bitvec[nextblock/8] = bitvec[nextblock/8] & (~bytechk[nextblock%8]);
						}
						//------------------18 readonly--------------------------
						
						
						nextblock = 0;
						cur.next = hfentry->sblock;
						//------------------19------------------------------
						if(isBoth){
							for(n=0;n<rfosdisk.numdisk;n++){
								nextblock = 0;
								cur.next = hfentry->sblock;
								for(i = 0;i<dataarr->len;i++){
									if((nextblock+1)-cur.next){				
										fseeko64(disk[n],cur.next*vblock.blocksize,SEEK_SET);
									}
									nextblock = cur.next;
									cur = g_array_index(dataarr,DBLOCK,i);
									fwrite(&cur,vblock.blocksize,1,disk[n]);
								}
							}
						}
						else{
							for(i = 0;i<dataarr->len;i++){
								if((nextblock+1)-cur.next){				
									fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
								}
							nextblock = cur.next;
							cur = g_array_index(dataarr,DBLOCK,i);
							fwrite(&cur,vblock.blocksize,1,disk[primary]);
							}
						}
						//------------------19------------------------------
						fe.atime = time(NULL);
						fe.valid = 1;
						fe.sblock = hfentry->sblock;
					
					vblock.free = vblock.free + (ceil(hfentry->size*1.0/(vblock.blocksize-4)) - ceil(fe.size*1.0/(vblock.blocksize-4)));
						hfentry->size = fe.size;
						hfentry->atime = fe.atime;

					//----------------------------20----------------
					if(isBoth){
						for(n=0;n<rfosdisk.numdisk;n++){
					fseeko64(disk[n],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
						fseeko64(disk[n],0,SEEK_SET);
						fwrite(&vblock,vblock.blocksize,1,disk[n]);	
						fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[n]);				
						fclose(disk[n]);
						}
					}
					else{
					fseeko64(disk[primary],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
						fseeko64(disk[primary],0,SEEK_SET);
						fwrite(&vblock,vblock.blocksize,1,disk[primary]);	
						fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[primary]);				
						fclose(disk[primary]);
					}
					//----------------------------20----------------	
										
					rfosRemOpenForWrite(p->key);
					g_printf("Case htb found with less block size req\n");	
					g_printf("KEY => %s\n",fe.key);
					g_printf("SIZE => %d\n",fe.size);
					g_printf("ATIME => %ld\n",fe.atime);
					g_printf("SBLOCK => %d\n",fe.sblock);
					g_printf("VALID => %d\n",fe.valid);				
					}
					fclose(fp);remOpenForRead(fst.st_ino);			
				}
				else{
				//Case file not valid
				//Do something similar case HTB not found but this time you can write desired file entry to desired location faster. 
					//----------------lockforkey--------------
					if(rfosIsOpenForRead(p->key)||rfosIsOpenForWrite(p->key)){
						err = EAGAIN;fclose(fp);remOpenForRead(fst.st_ino);goto putchkpt;
					}
					else{
						RFOSOFT_ENT rent;
						for(i = 0;i<8;i++)
							rent.key[i] = p->key[i];
						rent.key[8] = '\0';
						rent.mode = 'W';
 						g_array_append_val(rfosoft,rent);
					}
					//----------------------------------------					
					for(i=0;i<8;i++)
						fe.key[i] = p->key[i];
					fe.size = fst.st_size;
					fe.key[8] = '\0';
					if(ceil(fe.size*1.0 / (vblock.blocksize-4)) > vblock.free){
						fclose(fp);err = ENOSPC;
						remOpenForRead(fst.st_ino);rfosRemOpenForWrite(p->key);goto putchkpt;
					} 
					//Check for avail. block
					j = ((vblock.bitvec_bl+vblock.file_ent_bl+1)/8)*8;
					for(i = (vblock.bitvec_bl+vblock.file_ent_bl+1)/8;;i++){
						for(k=0;k<8;k++){
							if(j>=vblock.numblock || feof(fp))goto freeblkrwchkpt2; //0
	 						if((bitvec[i] & bytechk[k]) == 0){
								if(isFirst){
								cur.next = 0;
								fread(cur.data,1,desiresize,fp);
								blockloc = (i*8)+k;
								isFirst = FALSE;
								}
								else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr,cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;			
								}
							blockwrct++;
							bitvec[i] = bitvec[i] | bytechk[k];
							}
							j++;
						}
					}
					freeblkrwchkpt2:
					fclose(fp);
					remOpenForRead(fst.st_ino);
					g_array_append_val(dataarr,cur);
					cur.next = fe.sblock = blockloc;
					nextblock = 0;

					//---------------------6-------------------------
					for(n=0;n<rfosdisk.numdisk;n++)
						disk[n] = fopen64(rfosdisk.disk[n],"rb+");
					//---------------------6-------------------------
					if(disk[0]&&disk[1]){
						primary = 0;isBoth = TRUE;
					}
					else if(disk[0]&&!disk[1])
						primary = 0;
					else
						primary = 1;

					//---------------------7-------------------------
					if(isBoth){	
						for(n=0;n<rfosdisk.numdisk;n++){
							cur.next = fe.sblock = blockloc;
							nextblock = 0;			
							for(i = 0;i<dataarr->len;i++){
								if((nextblock+1)-cur.next){				
									fseeko64(disk[n],cur.next*vblock.blocksize,SEEK_SET);
								}
								nextblock = cur.next;
								cur = g_array_index(dataarr,DBLOCK,i);
								fwrite(&cur,vblock.blocksize,1,disk[n]);
							}
						}
					}
					else{
						for(i = 0;i<dataarr->len;i++){
							if((nextblock+1)-cur.next){				
								fseeko64(disk[primary],cur.next*vblock.blocksize,SEEK_SET);
							}
							nextblock = cur.next;
							cur = g_array_index(dataarr,DBLOCK,i);
							fwrite(&cur,vblock.blocksize,1,disk[primary]);
						}						
					}
					//---------------------7-------------------------
					fe.valid = 1;
					fe.atime = time(NULL);
				
					vblock.free = vblock.free - blockwrct;
					vblock.inv_file = vblock.inv_file - 1;
					
					hfentry->size = fe.size;
					hfentry->atime = fe.atime;
					hfentry->sblock = fe.sblock;
					hfentry->valid = 1;
					//-------------------8----------------------------
					if(isBoth){
						for(n = 0;n<rfosdisk.numdisk;n++){
							fseeko64(disk[n],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
							fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
							fseeko64(disk[n],0,SEEK_SET);
							fwrite(&vblock,vblock.blocksize,1,disk[n]);
							fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[n]);
							fclose(disk[n]);
						}
					}
					else{
						fseeko64(disk[primary],((vblock.bitvec_bl+1)*vblock.blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
						fseeko64(disk[primary],0,SEEK_SET);
						fwrite(&vblock,vblock.blocksize,1,disk[primary]);
						fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[primary]);
						fclose(disk[primary]);
					}
					//-------------------8----------------------------
					g_printf("Case invalid htb found\n");
					g_printf("KEY => %s\n",fe.key);
					g_printf("SIZE => %d\n",fe.size);
					g_printf("ATIME => %ld\n",fe.atime);
					g_printf("SBLOCK => %d\n",fe.sblock);
					g_printf("VALID => %d\n",fe.valid);

					rfosRemOpenForWrite(p->key);
				}
			}
	    		err = 0;
		}
	}
putchkpt:
rfos_complete_put(p->obj,p->inv,err);
/*DO GARBAGE COLLECTION*/
g_array_free(dataarr,TRUE);
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
else if(strlen(p->key) != 8 || (!isAlnumStr(p->key)))err = ENAMETOOLONG;
else if((fentry = g_hash_table_lookup(filetable,p->key)) != NULL){
	if(!fentry->valid)err = ENOENT;
	else{
		size = fentry->size;
		atime = fentry->atime;
		err = 0;
	    }
	}
else
	err = ENOENT;
rfos_complete_stat(p->obj,p->inv,size,atime,err);
//GARBAGE COLLECTION
g_free(p->key);
g_free(p);
pthread_exit(0);
}

void *do_handle_remove(void *pa){
guint err;int i,primary;gboolean isBoth = FALSE;
key_param *p = (key_param*)pa;
FILE_ENT_INMEM *fentry;
FILE_ENT fe;
FILE *disk[rfosdisk.numdisk];
for(i = 0;i<rfosdisk.numdisk;i++)
	disk[i] = NULL;
guint32 delcnt = 0;
if(!ready)err = EBUSY;
else if(strlen(p->key)!=8 || (!isAlnumStr(p->key)))err = ENAMETOOLONG;
else{
	if((fentry = g_hash_table_lookup(filetable,p->key)) != NULL){
		if(!fentry->valid)err = ENOENT; // if file entry is already invalid return ENOENT
		else{
			//----------------lockforkey--------------
			if(rfosIsOpenForRead(p->key)||rfosIsOpenForWrite(p->key)){
				err = EAGAIN;goto remchkpt;
			}
			else{
				RFOSOFT_ENT rent;
				for(i = 0;i<8;i++)
					rent.key[i] = p->key[i];
				rent.key[8] = '\0';
				rent.mode = 'W';
 				g_array_append_val(rfosoft,rent);
			}
			//----------------------------------------
		
			//********************************************
			for(i = 0;i <rfosdisk.numdisk;i++)		
				disk[i] = fopen64(rfosdisk.disk[i],"rb+");
			//********************************************
			if(disk[0]&&disk[1]){
				primary = 0;isBoth = TRUE;
			}
			else if(disk[0]&&!disk[1])
				primary = 0;
			else
				primary = 1;			
			DBLOCK data;
			data.next = fentry->sblock;

			//--------------------------------------------
			do{
				bitvec[data.next/8] = bitvec[data.next/8] & (~bytechk[data.next%8]);
				fseeko64(disk[primary],(data.next)*vblock.blocksize,SEEK_SET);
				fread(&data,vblock.blocksize,1,disk[primary]);
				delcnt++;
			}while(data.next);
			//--------------------------------------------
			//update valid properties
			fe.size = 0;
			int i;
			for(i =0;i<8;i++)
			fe.key[i] = fentry->key[i];
			fe.key[8] = '\0';
			fe.atime = 0;
			fe.sblock = 0;
			fe.valid = 0;fentry->valid = 0;
	
			//write data back to disk
			/*vblock.numfile--;*/
			vblock.inv_file++;vblock.free+=delcnt;
			
			if(isBoth)
			for(i = 0;i<rfosdisk.numdisk;i++){
				fseeko64(disk[i],0,SEEK_SET);
				fwrite(&vblock,vblock.blocksize,1,disk[i]);
				fseeko64(disk[i],(((vblock.bitvec_bl+1)*vblock.blocksize) +(fentry->fileno*sizeof(FILE_ENT))),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk[i]);
				fseeko64(disk[i],vblock.blocksize,SEEK_SET);
				fwrite(bitvec,1,(vblock.bitvec_bl*vblock.blocksize),disk[i]);		
				fclose(disk[i]);
			}
			else{
				fseeko64(disk[primary],0,SEEK_SET);
				fwrite(&vblock,vblock.blocksize,1,disk[primary]);
				fseeko64(disk[primary],(((vblock.bitvec_bl+1)*vblock.blocksize) +(fentry->fileno*sizeof(FILE_ENT))),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk[primary]);
				fseeko64(disk[primary],vblock.blocksize,SEEK_SET);
				fwrite(bitvec,1,(vblock.bitvec_bl*vblock.blocksize),disk[primary]);		
				fclose(disk[primary]);
			}
			rfosRemOpenForWrite(p->key);
			err = 0;
		}		
	}
	else
		err = ENOENT;	
}
remchkpt:
rfos_complete_remove(p->obj,p->inv,err);
//DO GARBAGE COLLECTION
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
	pthread_t worker;
	key_param *p = g_new(key_param,1);
	p->obj = object;
	p->inv = invocation;
	p->key = g_strdup(key);
	pthread_create(&worker,NULL,do_handle_remove,p);
	return TRUE;
}

static gboolean on_handle_search(
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key,
    const gchar *outpath){
	pthread_t worker;
	getput_param *p = g_new(getput_param,1);
	p->obj = object;
	p->inv = invocation;
	p->key = g_strdup(key);
	p->path = g_strdup(outpath);
    pthread_create(&worker,NULL,do_handle_search,p);
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
struct stat64 s,s2;
guint32 buffersize = 65536;
char buf[buffersize];
FILE *disk[rfosdisk.numdisk];
int n,pass=0;
for(n = 0;n<rfosdisk.numdisk;n++){
	disk[n] = fopen64(rfosdisk.disk[n],"rb+");
	if(!disk[n]){
		g_printf("OPEN DISK FAILED. -- Image disk is not found.\n");
		exit(1);
	}
}

///    1   1 
//     d1  d0	
for(n = 0;n < rfosdisk.numdisk;n++)
{
	fread(&vblock,sizeof(VCB),1,disk[n]);
	if(vblock.numblock != 0) pass = pass | (n+1);
}

	if(pass == 0){ // case both disk has no data
		g_printf("FILE SYSTEM NOT FOUND SERVICE WILL FORMAT...\n");
		stat64(rfosdisk.disk[0],&s);
		vblock.diskno = 0;
		vblock.blocksize = 1024;
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
	
	for(n = 0;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],0,SEEK_SET);
		fwrite(&vblock,vblock.blocksize,1,disk[n]);
		fwrite(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[n]);
	}
	g_printf("FORMAT COMPLETE!\n");
}
	else if(pass == 1 && rfosdisk.numdisk == 2){ // case disk0 OK but disk1 is not.
		//Relay data from disk0 to disk1 and continue initialized RFOS
		g_printf("Relay data from disk0 to disk1...\n");
		stat64(rfosdisk.disk[0],&s);
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[1],0,SEEK_SET);
		while(TRUE){
			fread(buf,1,buffersize,disk[0]);
			if(!feof(disk[0])){
				fwrite(buf,1,buffersize,disk[1]);
			}
			else{
				if(s.st_size % buffersize != 0)
					fwrite(buf,1,s.st_size % buffersize,disk[1]);
				break;
			}
			
		}

		fseeko64(disk[0],0,SEEK_SET);
		fread(&vblock,sizeof(VCB),1,disk[0]);
		goto readdatapt;
	}
	else if(pass == 2 && rfosdisk.numdisk == 2){// case disk1 OK but disk0 is not.
		//Relay data from disk1 to disk0 and continue initialized RFOS
		g_printf("Relay data from disk1 to disk0...\n");
		stat64(rfosdisk.disk[1],&s);
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[1],0,SEEK_SET);
		while(TRUE){
			fread(buf,1,buffersize,disk[1]);
			if(!feof(disk[1])){
				fwrite(buf,1,buffersize,disk[0]);
			}
			else{
				if(s.st_size % buffersize != 0)
					fwrite(buf,1,s.st_size % buffersize,disk[0]);
				break;
			}
			
		}

		fseeko64(disk[1],0,SEEK_SET);
		fread(&vblock,sizeof(VCB),1,disk[1]);
		goto readdatapt;
	}
	else{ //if both disk is success to read,default is read data from disk0
	readdatapt:
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
	fread(bitvec,1,vblock.bitvec_bl*vblock.blocksize,disk[0]);
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
	fseeko64(disk[0],feoff*vblock.blocksize,SEEK_SET);
	for(i=0;i<vblock.numfile;i++){
		fread(&fe,sizeof(FILE_ENT),1,disk[0]);
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

for(n = 0;n <rfosdisk.numdisk;n++)
fclose(disk[n]);
desiresize = vblock.blocksize-4;
ready = 1;
pthread_exit(0);
}

int main (int argc,char **argv)
{
pthread_t test;
actoft = g_array_new(TRUE,FALSE,sizeof(AOF_ENT));
rfosoft = g_array_new(TRUE,FALSE,sizeof(RFOSOFT_ENT));


if(argc > 1){
rfosdisk.numdisk = argc - 1;
rfosdisk.disk = g_new(gchar *,argc -1);
	if(argc == 2){
		rfosdisk.disk[0] = g_strdup(argv[1]);
	}
	else if(argc == 3){
		rfosdisk.disk[0] = g_strdup(argv[1]);
		rfosdisk.disk[1] = g_strdup(argv[2]);
	}
	else if(argc == 4){
		rfosdisk.disk[0] = g_strdup(argv[1]);
		rfosdisk.disk[1] = g_strdup(argv[2]);
		rfosdisk.disk[2] = g_strdup(argv[3]);
	}
	else{
		rfosdisk.disk[0] = g_strdup(argv[1]);
		rfosdisk.disk[1] = g_strdup(argv[2]);
		rfosdisk.disk[2] = g_strdup(argv[3]);
		rfosdisk.disk[3] = g_strdup(argv[4]);
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
//pthread_create(&test1,NULL,checkexist,NULL); 
   g_main_loop_run (loop);
}
else{
g_printf("Usage:./rfos-svc [disk1-name] [disk2-name] [disk3-name] [disk4-name]\n"); 
} 
   return 0;
}
