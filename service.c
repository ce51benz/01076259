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
gchar *bitvec[2];
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

VCB vblock[2];
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
guint err;int i,primary[2],pri=0;gboolean isBoth[2] = {FALSE,FALSE};
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
				primary[0] = 0;isBoth[0] = TRUE;
			}
			else if(disk[0]&&!disk[1])
				primary[0] = 0;
			else
				primary[0] = 1;
			
			if(disk[2]&&disk[3]){
				primary[1] = 2;isBoth[1] = TRUE;
			}
			else if(disk[2]&&!disk[3])
				primary[1] = 2;
			else
				primary[1] = 3;
			DBLOCK cur;
			cur.next = entry->sblock;
			nextblock = 0;
			//----------------------------
			if(entry->size == 0)goto bypassgetblkpt;
			if(cur.next >= vblock[0].numblock){
				cur.next = cur.next - vblock[0].numblock;
				pri = 1;
			}
			while(TRUE){
				//=============================================
				if((nextblock+1)-cur.next)
					fseeko64(disk[primary[pri]],cur.next*vblock[0].blocksize,SEEK_SET);
				nextblock = cur.next;
				fread(&cur,vblock[0].blocksize,1,disk[primary[pri]]);
				//=============================================

				//9999999999999999999999999999999999999999999999999999999999
				if(!cur.next){
					if(entry->size % desiresize != 0){
						fwrite(cur.data,1,entry->size % desiresize,fp);
						if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth[0])
								for(i = 0;i<2;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary[0]]);
							if(rfosdisk.numdisk > 2){
								if(isBoth[1])
									for(i = 2;i<rfosdisk.numdisk;i++)
										fclose(disk[i]);
								else
									fclose(disk[primary[1]]);
							}
						remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
					}
					else{
						fwrite(cur.data,1,desiresize,fp);
					if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth[0])
								for(i = 0;i<2;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary[0]]);
							if(rfosdisk.numdisk > 2){
								if(isBoth[1])
									for(i = 2;i<rfosdisk.numdisk;i++)
										fclose(disk[i]);
								else
									fclose(disk[primary[1]]);
							}
						remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
					    }
					break;
				}
				else if(cur.next >= vblock[0].numblock && !pri){
					//write current data and change disk to read data from another disk with the change of
					//cur.next by subtract from numblock and change primary disk to read from.
					fwrite(cur.data,1,desiresize,fp);
					if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth[0])
								for(i = 0;i<2;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary[0]]);
							if(rfosdisk.numdisk > 2){
								if(isBoth[1])
									for(i = 2;i<rfosdisk.numdisk;i++)
										fclose(disk[i]);
								else
									fclose(disk[primary[1]]);
							}
					remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
					cur.next = cur.next - vblock[0].numblock;
					nextblock = 0;
					pri = 1;
				}
				else{
					fwrite(cur.data,1,desiresize,fp);
					if(ferror(fp)){err=errno;fclose(fp);
							if(isBoth[0])
								for(i = 0;i<2;i++)
									fclose(disk[i]);
							else
								fclose(disk[primary[0]]);
							if(rfosdisk.numdisk > 2){
								if(isBoth[1])
									for(i = 2;i<rfosdisk.numdisk;i++)
										fclose(disk[i]);
								else
									fclose(disk[primary[1]]);
							}
					remOpenForWrite(fst.st_ino);rfosRemOpenForRead(p->key);goto getchkpt;}
				    }
				//9999999999999999999999999999999999999999999999999999999999
				
				//Update atime and write it back in disk to proper location (check file seq no and edit it without any confilct)
			}
			bypassgetblkpt:
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
			if(isBoth[0]){
				for(i=0;i<2;i++){
		        	fseeko64(disk[i],(((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(entry->fileno*sizeof(FILE_ENT))),SEEK_SET);		
				fwrite(&fe,sizeof(FILE_ENT),1,disk[i]);
				fclose(disk[i]);
				}
			}
			else    {
				fseeko64(disk[primary[0]],(((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(entry->fileno*sizeof(FILE_ENT))),SEEK_SET);			
				fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
				fclose(disk[primary[0]]);
				}
			if(rfosdisk.numdisk > 2){
				if(isBoth[1])
					for(i = 2;i<rfosdisk.numdisk;i++)
						fclose(disk[i]);
				else
					fclose(disk[primary[1]]);
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
GArray *dataarr[2];
DBLOCK cur;int pri=0;
guint32 i,blockloc,j,nextblock,k,primary[2],n,blockwrct=0;
gboolean isBoth[2] = {FALSE,FALSE};
FILE_ENT_INMEM *hfentry;FILE_ENT fe;
FILE *disk[rfosdisk.numdisk];
for(i=0;i<rfosdisk.numdisk;i++)
	disk[i] = NULL;
getput_param *p = (getput_param*)pa;
dataarr[0] = g_array_new(TRUE,FALSE,sizeof(DBLOCK));
dataarr[1] = g_array_new(TRUE,FALSE,sizeof(DBLOCK));
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
		FILE *fp = fopen64(p->path,"rb");
				
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
				if(rfosdisk.numdisk < 2){ 
					if(ceil(fe.size*1.0 / desiresize) > vblock[0].free){
						fclose(fp);
						err = ENOSPC;
						remOpenForRead(fst.st_ino);
						rfosRemOpenForWrite(p->key);
						goto putchkpt;
					}
				}
				else{
					if(ceil(fe.size*1.0 / desiresize) > (vblock[0].free+vblock[1].free)){
						fclose(fp);
						err = ENOSPC;
						remOpenForRead(fst.st_ino);
						rfosRemOpenForWrite(p->key);
						goto putchkpt;
					}
				}
				
				
				if(fe.size == 0){ //if file is size 0 byte no block is allocated.
					fe.sblock=0;
					fclose(fp);
					remOpenForRead(fst.st_ino);
					for(n = 0;n<rfosdisk.numdisk;n++)
						disk[n] = fopen64(rfosdisk.disk[n],"rb+");
					if(disk[0]&&disk[1]){
						primary[0] = 0;isBoth[0] = TRUE;
					}
					else if(disk[0]&&!disk[1])
						primary[0] = 0;
					else
						primary[0] = 1;
			
					if(disk[2]&&disk[3]){
						primary[1] = 2;isBoth[1] = TRUE;
					}
					else if(disk[2]&&!disk[3])
						primary[1] = 2;
					else
						primary[1] = 3;
					goto bypassallocblkpt0;
				}
				if(!vblock[0].free)goto disk23wrpt;
				//=====================================================================
				j = ((vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8)*8;
				for(i = (vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8;;i++){
					for(k=0;k<8;k++){
						if(j>=vblock[0].numblock)goto disk23wrpt; //0
 						if((bitvec[0][i] & bytechk[k]) == 0){
							if(isFirst){
								cur.next = 0;
								fread(cur.data,1,desiresize,fp);
								if(feof(fp)){
									blockloc = (i*8)+k;
									bitvec[0][i] = bitvec[0][i] | bytechk[k];
									goto enddisk01wrpt;
								}
								blockloc = (i*8)+k;
								isFirst = FALSE;
							}
							else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr[0],cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;
								if(feof(fp)){
									if(fe.size % desiresize != 0){
										bitvec[0][i] = bitvec[0][i] | bytechk[k];
										goto enddisk01wrpt;
									}
									else{
										cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
										cur.next = 0;
										g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
										goto enddisk01wrpt;
									}
								}
								
							}
						bitvec[0][i] = bitvec[0][i] | bytechk[k];
						}
						j++;
					}
				}
				//=====================================================================
				disk23wrpt:
				isFirst = TRUE;
				j = ((vblock[1].bitvec_bl+1)/8)*8;
				for(i = (vblock[1].bitvec_bl+1)/8;;i++){
					for(k=0;k<8;k++){
						if(j>=vblock[1].numblock)goto enddisk23wrpt;
 						if((bitvec[1][i] & bytechk[k]) == 0){
							if(isFirst){
								if(!dataarr[0]->len){
									cur.next = 0;
									fread(cur.data,1,desiresize,fp);
									if(feof(fp)){
										blockloc = (i*8)+k;
										bitvec[1][i] = bitvec[1][i] | bytechk[k];
										goto enddisk23wrpt;
									}
									blockloc = ((i*8)+k) + vblock[0].numblock;
								}
								else{
									//*************************************
									cur.next = ((i*8)+k) + vblock[0].numblock;
									//*************************************
									g_array_append_val(dataarr[0],cur);
									fread(cur.data,1,desiresize,fp);
									cur.next = 0;
									if(feof(fp)){
										if(fe.size % desiresize != 0){
											bitvec[1][i] = bitvec[1][i] | bytechk[k];
											goto enddisk23wrpt;
										}
										else{
											cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
											cur.next = 0;
											g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
											goto enddisk01wrpt;
										}
									}	
								}
								isFirst = FALSE;
							}
							else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr[1],cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;
								if(feof(fp)){
									if(fe.size % desiresize != 0){
										bitvec[1][i] = bitvec[1][i] | bytechk[k];
										goto enddisk23wrpt;
									}
									else{
										cur = g_array_index(dataarr[1],DBLOCK,(dataarr[1]->len-1));
										cur.next = 0;
										g_array_remove_index(dataarr[1],(dataarr[1]->len-1));
										goto enddisk23wrpt;
									}
								}
								
							}
						bitvec[1][i] = bitvec[1][i] | bytechk[k];
						}
						j++;
					}
				}								
				enddisk23wrpt:
				g_array_append_val(dataarr[1],cur);
				goto freeblkrwchkpt;

				enddisk01wrpt:
				//CONTINUED
				g_array_append_val(dataarr[0],cur);
				freeblkrwchkpt:
				fclose(fp);
				remOpenForRead(fst.st_ino);
				
				fe.sblock = blockloc;

				//-----------------------------------------------
				for(n = 0;n<rfosdisk.numdisk;n++)
					disk[n] = fopen64(rfosdisk.disk[n],"rb+");
				//-----------------------------------------------
				
				if(disk[0]&&disk[1]){
					primary[0] = 0;isBoth[0] = TRUE;
				}
				else if(disk[0]&&!disk[1])
					primary[0] = 0;
				else
					primary[0] = 1;
			
				if(disk[2]&&disk[3]){
					primary[1] = 2;isBoth[1] = TRUE;
				}
				else if(disk[2]&&!disk[3])
					primary[1] = 2;
				else
					primary[1] = 3;
				/////-------------------1------------------------------
				if(isBoth[0]){
					for(n = 0;n<2;n++){	
						cur.next = blockloc;
						nextblock = 0;
						for(i = 0;i<dataarr[0]->len;i++){
							if((nextblock+1)-cur.next){				
								fseeko64(disk[n],cur.next*vblock[0].blocksize,SEEK_SET);
							}
							nextblock = cur.next;
							cur = g_array_index(dataarr[0],DBLOCK,i);
							fwrite(&cur,vblock[0].blocksize,1,disk[n]);
						}
					}
				}
				else{
					cur.next = blockloc;
					nextblock = 0;
					for(i = 0;i<dataarr[0]->len;i++){
						if((nextblock+1)-cur.next){				
							fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
						}
						nextblock = cur.next;
						cur = g_array_index(dataarr[0],DBLOCK,i);
						fwrite(&cur,vblock[0].blocksize,1,disk[primary[0]]);
					}
				}
				
				if(isBoth[1]){
					for(n = 2;n<rfosdisk.numdisk;n++){
						if(dataarr[0]->len > 0){
							cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
							cur.next = cur.next - vblock[0].numblock;
							nextblock = 0;
						}
						else{
							cur.next = blockloc - vblock[0].numblock;
							nextblock = 0;
						}
						for(i = 0;i<dataarr[1]->len;i++){
							if((nextblock+1)-cur.next){				
								fseeko64(disk[n],cur.next*vblock[1].blocksize,SEEK_SET);
							}
							nextblock = cur.next;
							cur = g_array_index(dataarr[1],DBLOCK,i);
							fwrite(&cur,vblock[1].blocksize,1,disk[n]);
						}
					}
				}
				else{
					if(dataarr[0]->len > 0){
						cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
						cur.next = cur.next - vblock[0].numblock;
						nextblock = 0;
					}
					else{
						cur.next = blockloc - vblock[0].numblock;
						nextblock = 0;
					}
					for(i = 0;i<dataarr[1]->len;i++){
						if((nextblock+1)-cur.next){				
							fseeko64(disk[primary[1]],cur.next*vblock[1].blocksize,SEEK_SET);
						}
						nextblock = cur.next;
						cur = g_array_index(dataarr[1],DBLOCK,i);
						fwrite(&cur,vblock[1].blocksize,1,disk[primary[1]]);
					}
				}
					
				/////-------------------1------------------------------
				bypassallocblkpt0:
				fe.valid = 1;
				fe.atime = time(NULL);
				vblock[0].free = vblock[0].free - dataarr[0]->len;
				if(rfosdisk.numdisk > 2)
					vblock[1].free = vblock[1].free - dataarr[1]->len;
				//FILE LOCATION FOR AVAIL FILE ENT BLOCK AND WRITE VCB BACK
				if(!vblock[0].inv_file){
					if(ceil(((vblock[0].numfile-vblock[0].inv_file)*1.0*sizeof(FILE_ENT))/vblock[0].blocksize) < vblock[0].file_ent_bl)
					vblock[0].numfile = vblock[0].numfile+1;
					else{
						//will move this handle later...
						if(isBoth[0]){
							for(n = 0;n<rfosdisk.numdisk;n++)
								fclose(disk[n]);
						}
						else fclose(disk[primary[0]]);
						err = EPERM;
						rfosRemOpenForWrite(p->key);goto putchkpt;	
					}
					/////-------------------3------------------------------
					if(isBoth[0]){
						for(n = 0;n<2;n++){
							fseeko64(disk[n],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+((vblock[0].numfile-1)*sizeof(FILE_ENT)),SEEK_SET);
							fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
							fseeko64(disk[n],0,SEEK_SET);
							fwrite(&vblock[0],vblock[0].blocksize,1,disk[n]);
						}
					}
					else{
						fseeko64(disk[primary[0]],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+((vblock[0].numfile-1)*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
						fseeko64(disk[primary[0]],0,SEEK_SET);
						fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);
					}
					if(dataarr[1]->len > 0){
						if(isBoth[1]){
							for(n = 2;n<rfosdisk.numdisk;n++){
							fseeko64(disk[n],0,SEEK_SET);
							fwrite(&vblock[1],vblock[1].blocksize,1,disk[n]);
							}
						}
					else{
						fseeko64(disk[primary[1]],0,SEEK_SET);
						fwrite(&vblock[1],vblock[1].blocksize,1,disk[primary[1]]);
						}						
					}
					/////-------------------3------------------------------
					/*write entry FILE_ENT_INMEM to HTB*/
					FILE_ENT_INMEM *entry = g_new(FILE_ENT_INMEM,1);
					entry->key = g_strdup(fe.key);
					entry->size = fe.size;
					entry->atime = fe.atime;
					entry->sblock = fe.sblock;
					entry->valid = fe.valid;
					entry->fileno = vblock[0].numfile-1;
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
					vblock[0].inv_file = vblock[0].inv_file - 1;

					/////-------------------4------------------------------
					if(isBoth[0]){		
						for(n = 0;n<2;n++){
						fseeko64(disk[n],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(fentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
						fseeko64(disk[n],0,SEEK_SET);
						fwrite(&vblock[0],vblock[0].blocksize,1,disk[n]);
						}
					}
					else{
						fseeko64(disk[primary[0]],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(fentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
						fseeko64(disk[primary[0]],0,SEEK_SET);
						fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);
					}

					if(dataarr[1]->len > 0){
						if(isBoth[1]){
							for(n = 2;n<rfosdisk.numdisk;n++){
							fseeko64(disk[n],0,SEEK_SET);
							fwrite(&vblock[1],vblock[1].blocksize,1,disk[n]);
							}
						}
					else{
						fseeko64(disk[primary[1]],0,SEEK_SET);
						fwrite(&vblock[1],vblock[1].blocksize,1,disk[primary[1]]);
						}						
					}
					/////-------------------4------------------------------
				}
				
				//WRITE FREE BIT VEC TO IMG DISK
				/////-------------------2------------------------------
				if(fe.size != 0){
					if(dataarr[0]->len > 0){
						if(isBoth[0]){
							for(n = 0;n<2;n++){
								fseeko64(disk[n],vblock[0].blocksize,SEEK_SET);
								fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[n]);
							}
						}
						else{
							fseeko64(disk[primary[0]],vblock[0].blocksize,SEEK_SET);
							fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[primary[0]]);
						}
					}
					if(dataarr[1]->len > 0){
						if(isBoth[1]){
							for(n = 2;n<rfosdisk.numdisk;n++){
								fseeko64(disk[n],vblock[1].blocksize,SEEK_SET);
								fwrite(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[n]);
							}
						}
						else{
							fseeko64(disk[primary[1]],vblock[1].blocksize,SEEK_SET);
							fwrite(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[primary[1]]);
						}
					}
				}
				/////-------------------2------------------------------
				g_printf("NORMAL CASE\n");
				g_printf("KEY => %s\n",fe.key);
				g_printf("SIZE => %d\n",fe.size);
				g_printf("ATIME => %ld\n",fe.atime);
				g_printf("SBLOCK => %d\n",fe.sblock);
				g_printf("VALID => %d\n",fe.valid);
				
				/////-------------------5------------------------------
				if(isBoth[0]){
				for(n = 0;n<2;n++)
					fclose(disk[n]);
				}
				else fclose(disk[primary[0]]);
				
				if(rfosdisk.numdisk > 2){
					if(isBoth[1]){
						for(n = 2;n<rfosdisk.numdisk;n++)
							fclose(disk[n]);
					}
					else fclose(disk[primary[1]]);
				}
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
					if(ceil(hfentry->size*1.0/(vblock[0].blocksize-4)) == ceil(fe.size*1.0/(vblock[0].blocksize-4))){
						//this case can replace data instantly!
						//no need to update vblock
						 
						//---------------9------------------------------
						for(n = 0;n<rfosdisk.numdisk;n++)
							disk[n] = fopen64(rfosdisk.disk[n],"rb+");
						//---------------9------------------------------
						if(disk[0]&&disk[1]){
							primary[0] = 0;isBoth[0] = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary[0] = 0;
						else
							primary[0] = 1;
			
						if(disk[2]&&disk[3]){
							primary[1] = 2;isBoth[1] = TRUE;
						}
						else if(disk[2]&&!disk[3])
							primary[1] = 2;
						else
							primary[1] = 3;
						
						DBLOCK cur;
						nextblock =0;
						cur.next = hfentry->sblock;
						
						if(fe.size == 0){
							goto bypassallocblkpt1;
						}

						if(cur.next >= vblock[0].numblock){
							cur.next = cur.next - vblock[0].numblock;
							pri = 1;
						}
						//------------------10 readonly-----------------
						do{
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary[pri]],cur.next*vblock[pri].blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock[pri].blocksize,1,disk[primary[pri]]);
							fread(cur.data,1,desiresize,fp);
							g_array_append_val(dataarr[pri],cur);
							
							if(cur.next >= vblock[0].numblock && !pri){
								cur.next = cur.next - vblock[0].numblock;
								nextblock = 0;
								pri = 1;
							}
						}while(cur.next);
						//------------------10 readonly-----------------

						nextblock =0;
						cur.next = hfentry->sblock;
						//-------------------------11-------------------------
						if(isBoth[0]){
							for(n=0;n<2;n++){
								nextblock =0;
								cur.next = hfentry->sblock;
								for(i = 0;i<dataarr[0]->len;i++){
									if((nextblock+1)-cur.next)
										fseeko64(disk[n],cur.next*vblock[0].blocksize,SEEK_SET);
									nextblock = cur.next;
									cur = g_array_index(dataarr[0],DBLOCK,i);
									fwrite(&cur,vblock[0].blocksize,1,disk[n]);
								}
							}
						}
						else{
							nextblock =0;
							cur.next = hfentry->sblock;
							for(i = 0;i<dataarr[0]->len;i++){
								if((nextblock+1)-cur.next)
									fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
								nextblock = cur.next;
								cur = g_array_index(dataarr[0],DBLOCK,i);
								fwrite(&cur,vblock[0].blocksize,1,disk[primary[0]]);
							}
						}
						
				
						if(isBoth[1]){
							for(n=2;n<rfosdisk.numdisk;n++){
								if(dataarr[0]->len > 0){
									cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
									cur.next = cur.next - vblock[0].numblock;
									nextblock = 0;
								}
								else{
									cur.next = hfentry->sblock - vblock[0].numblock;
									nextblock = 0;
								}
								for(i = 0;i<dataarr[1]->len;i++){
									if((nextblock+1)-cur.next)
										fseeko64(disk[n],cur.next*vblock[1].blocksize,SEEK_SET);
									nextblock = cur.next;
									cur = g_array_index(dataarr[1],DBLOCK,i);
									fwrite(&cur,vblock[1].blocksize,1,disk[n]);
								}
							}
						}
						else{
							if(dataarr[0]->len > 0){
								cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
								cur.next = cur.next - vblock[0].numblock;
								nextblock = 0;
							}
							else{
								cur.next = hfentry->sblock - vblock[0].numblock;
								nextblock = 0;
							}
							for(i = 0;i<dataarr[1]->len;i++){
								if((nextblock+1)-cur.next)
									fseeko64(disk[primary[1]],cur.next*vblock[1].blocksize,SEEK_SET);
								nextblock = cur.next;
								cur = g_array_index(dataarr[1],DBLOCK,i);
								fwrite(&cur,vblock[1].blocksize,1,disk[primary[1]]);
							}
						}

						
						//-------------------------11-------------------------
						//Update fe block..... and write back!
						bypassallocblkpt1:	
						fe.atime = time(NULL);
						fe.valid = 1;
						fe.sblock = hfentry->sblock;
					 	hfentry->size = fe.size;
						hfentry->atime = fe.atime;
					//-----------------------12-------------------------------
					if(isBoth[0]){			
					for(n=0;n<2;n++){	
	fseeko64(disk[n],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);					
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);					
						fclose(disk[n]);
						}
					}
					else{
	fseeko64(disk[primary[0]],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);					
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);					
						fclose(disk[primary[0]]);
					}

					if(rfosdisk.numdisk > 2){
					if(isBoth[1]){
						for(n = 2;n<rfosdisk.numdisk;n++)
							fclose(disk[n]);
					}
					else fclose(disk[primary[1]]);
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
					else if(ceil(hfentry->size*1.0/(vblock[0].blocksize-4)) < ceil(fe.size*1.0/(vblock[0].blocksize-4))){
					//this case must allocate additional block via find free block...
					//Use similar procedure of normal case and do it!
					//Update block for decreased free value,bitvec and fe block
						if((ceil(fe.size*1.0 / (vblock[0].blocksize-4))-ceil(hfentry->size*1.0/(vblock[0].blocksize-4))) > vblock[0].free){
							fclose(fp);err = ENOSPC;
							remOpenForRead(fst.st_ino);rfosRemOpenForWrite(p->key);goto putchkpt;			
						}
						//------------------thirtheen-----------------
						for(n = 0;n<rfosdisk.numdisk;n++)
							disk[n] = fopen(rfosdisk.disk[n],"rb+");
						//------------------thirtheen-----------------
						if(disk[0]&&disk[1]){
							primary[0] = 0;isBoth[0] = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary[0] = 0;
						else
							primary[0] = 1;
			
						if(disk[2]&&disk[3]){
							primary[1] = 2;isBoth[1] = TRUE;
						}
						else if(disk[2]&&!disk[3])
							primary[1] = 2;
						else
							primary[1] = 3;
						
						cur.next = hfentry->sblock;
						nextblock = 0;
						if(!hfentry->size){
						j = ((vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8)*8;
				for(i = (vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8;;i++){
					for(k=0;k<8;k++){
						if(j>=vblock[0].numblock)goto freeblkrwchkpt3; //0
 						if((bitvec[0][i] & bytechk[k]) == 0){
							if(isFirst){
								cur.next = 0;
								fread(cur.data,1,desiresize,fp);
								if(feof(fp)){
									blockloc = (i*8)+k;
									bitvec[0][i] = bitvec[0][i] | bytechk[k];
									goto freeblkrwchkpt3;
								}
								blockloc = (i*8)+k;
								isFirst = FALSE;
							}
							else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr[0],cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;
								if(feof(fp)){
									if(fe.size % desiresize != 0){
										bitvec[0][i] = bitvec[0][i] | bytechk[k];
									}
									else{
										cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
										cur.next = 0;
										g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
									}
									goto freeblkrwchkpt3;
								}
							}
						bitvec[0][i] = bitvec[0][i] | bytechk[k];
						}
						j++;
					}
				}
						}
						//-----------------14 readonly----------------
						do{
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock[0].blocksize,1,disk[primary[0]]);
							fread(cur.data,1,desiresize,fp);
							if(cur.next)
								g_array_append_val(dataarr[0],cur);
						}while(cur.next);
						//-----------------14 readonly----------------
						
						//The last block has invalid block no.(has no.0)
						j = ((vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8)*8;
						for(i = (vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8;;i++){
							for(k=0;k<8;k++){
								if(j>=vblock[0].numblock)goto freeblkrwchkpt3; //0
 								if((bitvec[0][i] & bytechk[k]) == 0){	
									cur.next = (i*8)+k;
									g_array_append_val(dataarr[0],cur);
									cur.next = 0;
									fread(cur.data,1,desiresize,fp);
									if(feof(fp)){
										if(fe.size % desiresize != 0){
											blockwrct++;
											bitvec[0][i] = bitvec[0][i] | bytechk[k];
										}
										else{
											cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
											cur.next = 0;
											g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
										}
										goto freeblkrwchkpt3;
									}
									blockwrct++;
									bitvec[0][i] = bitvec[0][i] | bytechk[k];
								}
							j++;
							}
						}			
						freeblkrwchkpt3:
						g_array_append_val(dataarr[0],cur);
						//Write main data to disk
						if(hfentry->sblock != 0)
							cur.next = hfentry->sblock;
						else
							cur.next = hfentry->sblock = blockloc;
						nextblock = 0;
						
						//----------------------15--------------------
						if(isBoth[0]){
							for(n=0;n<rfosdisk.numdisk;n++){
								cur.next = hfentry->sblock;
								nextblock = 0;
								for(i = 0;i<dataarr[0]->len;i++){
									if((nextblock+1)-cur.next){				
										fseeko64(disk[n],cur.next*vblock[0].blocksize,SEEK_SET);
									}
									nextblock = cur.next;
									cur = g_array_index(dataarr[0],DBLOCK,i);
									fwrite(&cur,vblock[0].blocksize,1,disk[n]);
								}
							}
						}
						else{
							for(i = 0;i<dataarr[0]->len;i++){
								if((nextblock+1)-cur.next){				
									fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
								}
								nextblock = cur.next;
								cur = g_array_index(dataarr[0],DBLOCK,i);
								fwrite(&cur,vblock[0].blocksize,1,disk[primary[0]]);
							}
						}
						//----------------------15--------------------
						fe.atime = time(NULL);
						fe.sblock = hfentry->sblock;
						fe.valid = 1;
						
						if(hfentry->size != 0)
							vblock[0].free = vblock[0].free - blockwrct;
						else
							vblock[0].free = vblock[0].free - dataarr[0]->len;
						
						//--------------------16--------------------
						if(isBoth[0]){
							for(n = 0;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);				
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
						fseeko64(disk[n],0,SEEK_SET);
					fwrite(&vblock[0],vblock[0].blocksize,1,disk[n]);								
						fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[n]);
						fclose(disk[n]);
						}
						}
						else{
						fseeko64(disk[primary[0]],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);				
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
						fseeko64(disk[primary[0]],0,SEEK_SET);
					fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);								
						fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[primary[0]]);
						fclose(disk[primary[0]]);
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
							primary[0] = 0;isBoth[0] = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary[0] = 0;
						else
							primary[0] = 1;
			
						if(disk[2]&&disk[3]){
							primary[1] = 2;isBoth[1] = TRUE;
						}
						else if(disk[2]&&!disk[3])
							primary[1] = 2;
						else
							primary[1] = 3;
						
						DBLOCK cur;guint tempnext;
						cur.next = hfentry->sblock;
						nextblock = 0;
						if(!fe.size){
							tempnext = hfentry->sblock;
							goto bypassallocblkpt3;
						}
						//------------------18 readonly------------------------
						while(TRUE){
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock[0].blocksize,1,disk[primary[0]]);
							fread(cur.data,1,desiresize,fp);
							
							if(!feof(fp)){
								g_array_append_val(dataarr[0],cur);
							}
							else{
								if(fe.size % desiresize != 0){
									tempnext = cur.next;
									cur.next = 0;
									g_array_append_val(dataarr[0],cur);
								}
								else{
									tempnext = nextblock;
									cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
									cur.next = 0;
									g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
									g_array_append_val(dataarr[0],cur);
								}
								break;
							}
						}
						
						bypassallocblkpt3:
						cur.next = tempnext;
						nextblock = 0;
						while(cur.next){
							if((nextblock+1)-cur.next)
								fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
							nextblock = cur.next;
							fread(&cur,vblock[0].blocksize,1,disk[primary[0]]);
							bitvec[0][nextblock/8] = bitvec[0][nextblock/8] & (~bytechk[nextblock%8]);
						}
						//------------------18 readonly--------------------------
						if(!fe.size)goto bypassallocblkpt5;
						nextblock = 0;
						cur.next = hfentry->sblock;
						//------------------19------------------------------
						if(isBoth[0]){
							for(n=0;n<rfosdisk.numdisk;n++){
								nextblock = 0;
								cur.next = hfentry->sblock;
								for(i = 0;i<dataarr[0]->len;i++){
									if((nextblock+1)-cur.next){				
										fseeko64(disk[n],cur.next*vblock[0].blocksize,SEEK_SET);
									}
									nextblock = cur.next;
									cur = g_array_index(dataarr[0],DBLOCK,i);
									fwrite(&cur,vblock[0].blocksize,1,disk[n]);
								}
							}
						}
						else{
							for(i = 0;i<dataarr[0]->len;i++){
								if((nextblock+1)-cur.next){				
									fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
								}
							nextblock = cur.next;
							cur = g_array_index(dataarr[0],DBLOCK,i);
							fwrite(&cur,vblock[0].blocksize,1,disk[primary[0]]);
							}
						}
						//------------------19------------------------------
						bypassallocblkpt5:
						fe.atime = time(NULL);
						fe.valid = 1;
						if(fe.size != 0)
							fe.sblock = hfentry->sblock;
						else{
							fe.sblock = hfentry->sblock = 0;
						}
					
					vblock[0].free = vblock[0].free + (ceil(hfentry->size*1.0/(vblock[0].blocksize-4)) - ceil(fe.size*1.0/(vblock[0].blocksize-4)));
						hfentry->size = fe.size;
						hfentry->atime = fe.atime;

					//----------------------------20----------------
					if(isBoth[0]){
						for(n=0;n<rfosdisk.numdisk;n++){
					fseeko64(disk[n],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
						fseeko64(disk[n],0,SEEK_SET);
						fwrite(&vblock[0],vblock[0].blocksize,1,disk[n]);	
						fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[n]);				
						fclose(disk[n]);
						}
					}
					else{
					fseeko64(disk[primary[0]],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
						fseeko64(disk[primary[0]],0,SEEK_SET);
						fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);	
						fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[primary[0]]);				
						fclose(disk[primary[0]]);
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
					if(ceil(fe.size*1.0 / (vblock[0].blocksize-4)) > vblock[0].free){
						fclose(fp);err = ENOSPC;
						remOpenForRead(fst.st_ino);rfosRemOpenForWrite(p->key);goto putchkpt;
					}
					
					if(fe.size == 0){
						fe.sblock=0;
						fclose(fp);
						remOpenForRead(fst.st_ino);
						for(n = 0;n<rfosdisk.numdisk;n++)
							disk[n] = fopen64(rfosdisk.disk[n],"rb+");
						if(disk[0]&&disk[1]){
							primary[0] = 0;isBoth[0] = TRUE;
						}
						else if(disk[0]&&!disk[1])
							primary[0] = 0;
						else
							primary[0] = 1;
			
						if(disk[2]&&disk[3]){
							primary[1] = 2;isBoth[1] = TRUE;
						}
						else if(disk[2]&&!disk[3])
							primary[1] = 2;
						else
							primary[1] = 3;
						goto bypassallocblkpt2;
					}
					//Check for avail. block
					
				if(!vblock[0].free)goto disk23wrpt2;
				//=====================================================================
				j = ((vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8)*8;
				for(i = (vblock[0].bitvec_bl+vblock[0].file_ent_bl+1)/8;;i++){
					for(k=0;k<8;k++){
						if(j>=vblock[0].numblock)goto disk23wrpt2; //0
 						if((bitvec[0][i] & bytechk[k]) == 0){
							if(isFirst){
								cur.next = 0;
								fread(cur.data,1,desiresize,fp);
								if(feof(fp)){
									blockloc = (i*8)+k;
									bitvec[0][i] = bitvec[0][i] | bytechk[k];
									goto enddisk01wrpt2;
								}
								blockloc = (i*8)+k;
								isFirst = FALSE;
							}
							else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr[0],cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;
								if(feof(fp)){
									if(fe.size % desiresize != 0){
										bitvec[0][i] = bitvec[0][i] | bytechk[k];
										goto enddisk01wrpt2;
									}
									else{
										cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
										cur.next = 0;
										g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
										goto enddisk01wrpt2;
									}
								}
								
							}
						bitvec[0][i] = bitvec[0][i] | bytechk[k];
						}
						j++;
					}
				}
				//=====================================================================
				disk23wrpt2:
				isFirst = TRUE;
				j = ((vblock[1].bitvec_bl+1)/8)*8;
				for(i = (vblock[1].bitvec_bl+1)/8;;i++){
					for(k=0;k<8;k++){
						if(j>=vblock[1].numblock)goto enddisk23wrpt2;
 						if((bitvec[1][i] & bytechk[k]) == 0){
							if(isFirst){
								if(!dataarr[0]->len){
									cur.next = 0;
									fread(cur.data,1,desiresize,fp);
									if(feof(fp)){
										blockloc = (i*8)+k;
										bitvec[1][i] = bitvec[1][i] | bytechk[k];
										goto enddisk23wrpt2;
									}
									blockloc = ((i*8)+k) + vblock[0].numblock;
								}
								else{
									//*************************************
									cur.next = ((i*8)+k) + vblock[0].numblock;
									//*************************************
									g_array_append_val(dataarr[0],cur);
									fread(cur.data,1,desiresize,fp);
									cur.next = 0;
									if(feof(fp)){
										if(fe.size % desiresize != 0){
											bitvec[1][i] = bitvec[1][i] | bytechk[k];
											goto enddisk23wrpt2;
										}
										else{
											cur = g_array_index(dataarr[0],DBLOCK,(dataarr[0]->len-1));
											cur.next = 0;
											g_array_remove_index(dataarr[0],(dataarr[0]->len-1));
											goto enddisk01wrpt2;
										}
									}	
								}
								isFirst = FALSE;
							}
							else{
								cur.next = (i*8)+k;
								g_array_append_val(dataarr[1],cur);
								fread(cur.data,1,desiresize,fp);
								cur.next = 0;
								if(feof(fp)){
									if(fe.size % desiresize != 0){
										bitvec[1][i] = bitvec[1][i] | bytechk[k];
										goto enddisk23wrpt2;
									}
									else{
										cur = g_array_index(dataarr[1],DBLOCK,(dataarr[1]->len-1));
										cur.next = 0;
										g_array_remove_index(dataarr[1],(dataarr[1]->len-1));
										goto enddisk23wrpt2;
									}
								}
								
							}
						bitvec[1][i] = bitvec[1][i] | bytechk[k];
						}
						j++;
					}
				}								
				enddisk23wrpt2:
				g_array_append_val(dataarr[1],cur);
				goto freeblkrwchkpt2;

				enddisk01wrpt2:
				//CONTINUED
				g_array_append_val(dataarr[0],cur);
				freeblkrwchkpt2:
				fclose(fp);
				remOpenForRead(fst.st_ino);
				
				fe.sblock = blockloc;
				
					//---------------------6-------------------------
					for(n=0;n<rfosdisk.numdisk;n++)
						disk[n] = fopen64(rfosdisk.disk[n],"rb+");
					//---------------------6-------------------------
					if(disk[0]&&disk[1]){
						primary[0] = 0;isBoth[0] = TRUE;
					}
					else if(disk[0]&&!disk[1])
						primary[0] = 0;
					else
						primary[0] = 1;
			
					if(disk[2]&&disk[3]){
						primary[1] = 2;isBoth[1] = TRUE;
					}
					else if(disk[2]&&!disk[3])
						primary[1] = 2;
					else
						primary[1] = 3;

					//---------------------7-------------------------
					if(isBoth[0]){
					for(n = 0;n<2;n++){	
						cur.next = blockloc;
						nextblock = 0;
						for(i = 0;i<dataarr[0]->len;i++){
							if((nextblock+1)-cur.next){				
								fseeko64(disk[n],cur.next*vblock[0].blocksize,SEEK_SET);
							}
							nextblock = cur.next;
							cur = g_array_index(dataarr[0],DBLOCK,i);
							fwrite(&cur,vblock[0].blocksize,1,disk[n]);
						}
					}
				}
				else{
					cur.next = blockloc;
					nextblock = 0;
					for(i = 0;i<dataarr[0]->len;i++){
						if((nextblock+1)-cur.next){				
							fseeko64(disk[primary[0]],cur.next*vblock[0].blocksize,SEEK_SET);
						}
						nextblock = cur.next;
						cur = g_array_index(dataarr[0],DBLOCK,i);
						fwrite(&cur,vblock[0].blocksize,1,disk[primary[0]]);
					}
				}
				
				if(isBoth[1]){
					for(n = 2;n<rfosdisk.numdisk;n++){	
						cur.next = blockloc - vblock[0].numblock;
						nextblock = 0;
						for(i = 0;i<dataarr[1]->len;i++){
							if((nextblock+1)-cur.next){				
								fseeko64(disk[n],cur.next*vblock[1].blocksize,SEEK_SET);
							}
							nextblock = cur.next;
							cur = g_array_index(dataarr[1],DBLOCK,i);
							fwrite(&cur,vblock[1].blocksize,1,disk[n]);
						}
					}
				}
				else{
					cur.next = blockloc - vblock[0].numblock;
					nextblock = 0;
					for(i = 0;i<dataarr[1]->len;i++){
						if((nextblock+1)-cur.next){				
							fseeko64(disk[primary[1]],cur.next*vblock[1].blocksize,SEEK_SET);
						}
						nextblock = cur.next;
						cur = g_array_index(dataarr[1],DBLOCK,i);
						fwrite(&cur,vblock[1].blocksize,1,disk[primary[1]]);
					}
				}
					//---------------------7-------------------------
					bypassallocblkpt2:					
					fe.valid = 1;
					fe.atime = time(NULL);
				
					vblock[0].free = vblock[0].free - dataarr[0]->len;
					vblock[0].inv_file = vblock[0].inv_file - 1;
					
					if(dataarr[1]->len > 0)
						vblock[1].free = vblock[1].free - dataarr[1]->len;
					hfentry->size = fe.size;
					hfentry->atime = fe.atime;
					hfentry->sblock = fe.sblock;
					hfentry->valid = 1;
					//-------------------8----------------------------
					if(isBoth[0]){
						for(n = 0;n<2;n++){
							fseeko64(disk[n],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
							fwrite(&fe,sizeof(FILE_ENT),1,disk[n]);
							fseeko64(disk[n],0,SEEK_SET);
							fwrite(&vblock[0],vblock[0].blocksize,1,disk[n]);
							if(dataarr[0]->len > 0)
								fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[n]);
							fclose(disk[n]);
						}
					}
					else{
						fseeko64(disk[primary[0]],((vblock[0].bitvec_bl+1)*vblock[0].blocksize)+(hfentry->fileno*sizeof(FILE_ENT)),SEEK_SET);
						fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
						fseeko64(disk[primary[0]],0,SEEK_SET);
						fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);
						if(dataarr[0]->len > 0)
							fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[primary[0]]);
						fclose(disk[primary[0]]);
					}

					if(dataarr[1]->len > 0){
						if(isBoth[1]){
							for(n = 2;n<rfosdisk.numdisk;n++){
								fseeko64(disk[n],0,SEEK_SET);
								fwrite(&vblock[1],vblock[1].blocksize,1,disk[n]);
								fwrite(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[n]);
							}
						}
						else{
							fseeko64(disk[primary[1]],0,SEEK_SET);
							fwrite(&vblock[1],vblock[1].blocksize,1,disk[primary[1]]);
							fwrite(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[primary[1]]);
						}
					}

					if(rfosdisk.numdisk > 2){
						if(isBoth[1]){
							for(n = 2;n<rfosdisk.numdisk;n++)
								fclose(disk[n]);
						}
						else fclose(disk[primary[1]]);
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
g_array_free(dataarr[0],TRUE);g_array_free(dataarr[1],TRUE);
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
guint err;int i,primary[2];gboolean isBoth[2] = {FALSE,FALSE};
key_param *p = (key_param*)pa;
FILE_ENT_INMEM *fentry;
FILE_ENT fe;
FILE *disk[rfosdisk.numdisk];
for(i = 0;i<rfosdisk.numdisk;i++)
	disk[i] = NULL;
guint32 delcnt[2] = {0,0};
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
				primary[0] = 0;isBoth[0] = TRUE;
			}
			else if(disk[0]&&!disk[1])
				primary[0] = 0;
			else
				primary[0] = 1;
			
			if(disk[2]&&disk[3]){
				primary[1] = 2;isBoth[1] = TRUE;
			}
			else if(disk[2]&&!disk[3])
				primary[1] = 2;
			else
				primary[1] = 3;
			DBLOCK data;
			data.next = fentry->sblock;
			if(fentry->size == 0)
				goto bypassdeallocblkpt;
			if(data.next >= vblock[0].numblock)
				goto cleardisk23pt;
			//--------------------------------------------
			//clear block on disk01
			else if(rfosdisk.numdisk > 2){
				do{
					bitvec[0][data.next/8] = bitvec[0][data.next/8] & (~bytechk[data.next%8]);
					fseeko64(disk[primary[0]],(data.next)*vblock[0].blocksize,SEEK_SET);
					fread(&data,vblock[0].blocksize,1,disk[primary[0]]);
					delcnt[0]++;
					if(data.next >= vblock[0].numblock)break;
				}while(data.next);
			}
			else{
				do{
					bitvec[0][data.next/8] = bitvec[0][data.next/8] & (~bytechk[data.next%8]);
					fseeko64(disk[primary[0]],(data.next)*vblock[0].blocksize,SEEK_SET);
					fread(&data,vblock[0].blocksize,1,disk[primary[0]]);
					delcnt[0]++;
				}while(data.next);
			}
			cleardisk23pt:
			if(data.next){
				data.next = data.next - vblock[0].numblock;
				//clear block on disk23
				do{
					bitvec[1][data.next/8] = bitvec[1][data.next/8] & (~bytechk[data.next%8]);
					fseeko64(disk[primary[1]],(data.next)*vblock[1].blocksize,SEEK_SET);
					fread(&data,vblock[1].blocksize,1,disk[primary[1]]);
					delcnt[1]++;
				}while(data.next);
			}
			//--------------------------------------------
			//update valid properties
			bypassdeallocblkpt:
			fe.size = 0;
			int i;
			for(i =0;i<8;i++)
			fe.key[i] = fentry->key[i];
			fe.key[8] = '\0';
			fe.atime = 0;
			fe.sblock = 0;
			fe.valid = 0;fentry->valid = 0;
	
			//write data back to disk
			
			vblock[0].inv_file++;vblock[0].free+=delcnt[0];
			if(rfosdisk.numdisk > 2)
				vblock[1].free+=delcnt[1];
			
			if(isBoth[0] && isBoth[1])
			for(i = 0;i<rfosdisk.numdisk;i++){
				fseeko64(disk[i],0,SEEK_SET);
				fwrite(&vblock[i/2],vblock[i/2].blocksize,1,disk[i]);
				if(i<2){
				fseeko64(disk[i],(((vblock[0].bitvec_bl+1)*vblock[0].blocksize) +(fentry->fileno*sizeof(FILE_ENT))),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk[i]);
				}
				if(delcnt[i/2] != 0){
					fseeko64(disk[i],vblock[i/2].blocksize,SEEK_SET);
					fwrite(bitvec[i/2],1,(vblock[i/2].bitvec_bl*vblock[i/2].blocksize),disk[i]);	
				}
				fclose(disk[i]);
			}
			else if(!isBoth[0] && isBoth[1]){
				fseeko64(disk[primary[0]],0,SEEK_SET);
				fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);
				fseeko64(disk[primary[0]],(((vblock[0].bitvec_bl+1)*vblock[0].blocksize) +(fentry->fileno*sizeof(FILE_ENT))),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
				if(delcnt[0] != 0){
					fseeko64(disk[primary[0]],vblock[0].blocksize,SEEK_SET);
					fwrite(bitvec[0],1,(vblock[0].bitvec_bl*vblock[0].blocksize),disk[primary[0]]);
				}
				fclose(disk[primary[0]]);
							
				for(i = 2;i<rfosdisk.numdisk;i++){
					if(delcnt[1] != 0){
						fseeko64(disk[i],0,SEEK_SET);
						fwrite(&vblock[1],vblock[1].blocksize,1,disk[i]);
						fseeko64(disk[i],vblock[1].blocksize,SEEK_SET);
						fwrite(bitvec[1],1,(vblock[1].bitvec_bl*vblock[1].blocksize),disk[i]);	
					}
					fclose(disk[i]);
				}
			}
			
			else if(isBoth[0] && !isBoth[1]){
				for(i = 0;i<2;i++){
					fseeko64(disk[i],0,SEEK_SET);
					fwrite(&vblock[0],vblock[0].blocksize,1,disk[i]);
					fseeko64(disk[i],(((vblock[0].bitvec_bl+1)*vblock[0].blocksize) +(fentry->fileno*sizeof(FILE_ENT))),SEEK_SET);
					fwrite(&fe,sizeof(FILE_ENT),1,disk[i]);
					if(delcnt[0] != 0){
						fseeko64(disk[i],vblock[0].blocksize,SEEK_SET);
						fwrite(bitvec[0],1,(vblock[0].bitvec_bl*vblock[0].blocksize),disk[i]);	
					}
					fclose(disk[i]);
				}
				
				if(rfosdisk.numdisk > 2){
					if(delcnt[1] != 0){
						fseeko64(disk[primary[1]],0,SEEK_SET);
						fwrite(&vblock[1],vblock[1].blocksize,1,disk[primary[1]]);
						fseeko64(disk[primary[1]],vblock[1].blocksize,SEEK_SET);
						fwrite(bitvec[1],1,(vblock[1].bitvec_bl*vblock[1].blocksize),disk[primary[1]]);	
					}
					fclose(disk[primary[1]]);
				}
			}
			else{
				fseeko64(disk[primary[0]],0,SEEK_SET);
				fwrite(&vblock[0],vblock[0].blocksize,1,disk[primary[0]]);
				fseeko64(disk[primary[0]],(((vblock[0].bitvec_bl+1)*vblock[0].blocksize) +(fentry->fileno*sizeof(FILE_ENT))),SEEK_SET);
				fwrite(&fe,sizeof(FILE_ENT),1,disk[primary[0]]);
				if(delcnt[0] != 0){
					fseeko64(disk[primary[0]],vblock[0].blocksize,SEEK_SET);
					fwrite(bitvec[0],1,(vblock[0].bitvec_bl*vblock[0].blocksize),disk[primary[0]]);	
				}
				fclose(disk[primary[0]]);
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
guint32 i,j,k;
VCB tempvcb[4];
for(n = 0;n<rfosdisk.numdisk;n++){
	disk[n] = fopen64(rfosdisk.disk[n],"rb+");
	if(!disk[n]){
		g_printf("OPEN DISK FAILED. -- Image disk is not found.\n");
		exit(1);
	}
	
}

///    1   1 
//     d1  d0	

//1111
for(n = 0;n < rfosdisk.numdisk;n++)
{
	fread(&vblock[n/2],sizeof(VCB),1,disk[n]);
	if(vblock[n/2].numblock != 0) pass = pass | bytechk[7-n];
}

	if(pass == 0 && rfosdisk.numdisk <=2){ // case both disks has no data
		g_printf("FILE SYSTEM NOT FOUND SERVICE WILL FORMAT...\n");
		stat64(rfosdisk.disk[0],&s);
		vblock[0].diskno = 0;
		vblock[0].blocksize = 1024;
		vblock[0].numblock = s.st_size/vblock[0].blocksize;
		vblock[0].bitvec_bl = ceil((vblock[0].numblock*1.0)/8/vblock[0].blocksize);
		vblock[0].file_ent_bl = vblock[0].numblock*0.05;
		vblock[0].free = vblock[0].numblock-vblock[0].bitvec_bl-vblock[0].file_ent_bl-1;
		vblock[0].numfile = 0;
		vblock[0].inv_file = 0;
		bitvec[0] = g_new(gchar,vblock[0].bitvec_bl*vblock[0].blocksize);
		j=0;
		guint32 startblock = vblock[0].bitvec_bl+vblock[0].file_ent_bl+1;
		for(i = 0;;i++){
			for(k=0;k<8;k++){
				if(j>=startblock)goto filefmtchkpt;
				bitvec[0][i] = bitvec[0][i] | bytechk[k];
			j++;
		}
	}
	filefmtchkpt:
	
	for(n = 0;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],0,SEEK_SET);
		fwrite(&vblock[0],vblock[0].blocksize,1,disk[n]);
		fwrite(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[n]);
	}
	g_printf("FORMAT COMPLETE!\n");
}
	else if(pass == 0){
	//Case 3-4 disk has no data
	//Format first 2 disk like above case
	//Format last 2 disk by do below step
	//Set VCB to desire value for diskno,numblock,free,bitvec_bl,blocksize,file_ent_bl = 0,numfile=0,inv_file =0;
	//Set bitvec .... only 1st block and bitvec_bl block are allocated and write back to both disk
		g_printf("FILE SYSTEM NOT FOUND SERVICE WILL FORMAT...\n");
		stat64(rfosdisk.disk[0],&s);
		stat64(rfosdisk.disk[2],&s2);
		vblock[0].diskno = 0;
		vblock[0].blocksize = 1024;
		vblock[0].numblock = s.st_size/vblock[0].blocksize;
		vblock[0].bitvec_bl = ceil((vblock[0].numblock*1.0)/8/vblock[0].blocksize);
		vblock[0].file_ent_bl = vblock[0].numblock*0.05;
		vblock[0].free = vblock[0].numblock-vblock[0].bitvec_bl-vblock[0].file_ent_bl-1;
		vblock[0].numfile = 0;
		vblock[0].inv_file = 0;
		bitvec[0] = g_new(gchar,vblock[0].bitvec_bl*vblock[0].blocksize);
		j=0;
		guint32 startblock = vblock[0].bitvec_bl+vblock[0].file_ent_bl+1;
		for(i = 0;;i++){
			for(k=0;k<8;k++){
				if(j>=startblock)goto filefmtchkpt0;
				bitvec[0][i] = bitvec[0][i] | bytechk[k];
			j++;
			}
		}
	filefmtchkpt0:
		vblock[1].diskno = 1;
		vblock[1].blocksize = 1024;
		vblock[1].numblock = s2.st_size/vblock[1].blocksize;
		vblock[1].bitvec_bl = ceil((vblock[1].numblock*1.0)/8/vblock[1].blocksize);
		vblock[1].free = vblock[1].numblock-vblock[1].bitvec_bl-1;
		vblock[1].numfile = 0;
		vblock[1].inv_file = 0;
		vblock[1].file_ent_bl = 0;
		bitvec[1] = g_new(gchar,vblock[1].bitvec_bl*vblock[1].blocksize);
		j=0;
		startblock = vblock[1].bitvec_bl+1;
		for(i = 0;;i++){
			for(k=0;k<8;k++){
				if(j>=startblock)goto filefmtchkpt1;
				bitvec[1][i] = bitvec[1][i] | bytechk[k];
			j++;
			}
		}
	filefmtchkpt1:

	//write disk 1-4
	for(n = 0;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],0,SEEK_SET);
		fwrite(&vblock[n/2],vblock[n/2].blocksize,1,disk[n]);
		fwrite(bitvec[n/2],1,vblock[n/2].bitvec_bl*vblock[n/2].blocksize,disk[n]);
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
		fread(&vblock[0],sizeof(VCB),1,disk[0]);
		goto readdatapt;
	}
	else if(pass == 1 && rfosdisk.numdisk > 2){
 		// case disk0 OK but disk1 is not. with newly 2 disk are added.
		//Relay data from disk0 to disk1 with formatted of last 2 disk
		//and continue initialized RFOS
		passonechkpt:
		g_printf("Relay data from disk0 to disk1 with format last 1-2 disks...\n");
		stat64(rfosdisk.disk[0],&s);
		stat64(rfosdisk.disk[2],&s2);
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
		vblock[1].diskno = 1;
		vblock[1].blocksize = 1024;
		vblock[1].numblock = s2.st_size/vblock[1].blocksize;
		vblock[1].bitvec_bl = ceil((vblock[1].numblock*1.0)/8/vblock[1].blocksize);
		vblock[1].free = vblock[1].numblock-vblock[1].bitvec_bl-1;
		vblock[1].numfile = 0;
		vblock[1].inv_file = 0;
		vblock[1].file_ent_bl = 0;
		bitvec[1] = g_new(gchar,vblock[1].bitvec_bl*vblock[1].blocksize);
		j=0;
		guint32 startblock = vblock[1].bitvec_bl+1;
		for(i=0;;i++){
			for(k=0;k<8;k++){
				if(j>=startblock)goto filefmtchkpt2;
				bitvec[1][i] = bitvec[1][i] | bytechk[k];
			j++;
			}
		}
	filefmtchkpt2:

	//write disk 3-4
	for(n = 2;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],0,SEEK_SET);
		fwrite(&vblock[1],vblock[1].blocksize,1,disk[n]);
		fwrite(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[n]);
	}
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[2],0,SEEK_SET);
		fread(&vblock[0],sizeof(VCB),1,disk[0]);
		fread(&vblock[1],sizeof(VCB),1,disk[2]);		
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
		fread(&vblock[0],sizeof(VCB),1,disk[1]);
		goto readdatapt;
	}
	else if(pass == 2 && rfosdisk.numdisk > 2){
 		// case disk1 OK but disk0 is not. with newly 2 disk are added.
		//Relay data from disk1 to disk0 with formatted of last 2 disk
		//and continue initialized RFOS
		g_printf("Relay data from disk1 to disk0 with format last 1-2 disks...\n");
		stat64(rfosdisk.disk[1],&s);
		stat64(rfosdisk.disk[2],&s2);
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
		passtwochkpt:
		vblock[1].diskno = 1;
		vblock[1].blocksize = 1024;
		vblock[1].numblock = s2.st_size/vblock[1].blocksize;
		vblock[1].bitvec_bl = ceil((vblock[1].numblock*1.0)/8/vblock[1].blocksize);
		vblock[1].free = vblock[1].numblock-vblock[1].bitvec_bl-1;
		vblock[1].numfile = 0;
		vblock[1].inv_file = 0;
		vblock[1].file_ent_bl = 0;
		bitvec[1] = g_new(gchar,vblock[1].bitvec_bl*vblock[1].blocksize);
		j=0;
		guint32 startblock = vblock[1].bitvec_bl+1;
		for(i=0;;i++){
			for(k=0;k<8;k++){
				if(j>=startblock)goto filefmtchkpt3;
				bitvec[1][i] = bitvec[1][i] | bytechk[k];
			j++;
			}
		}
	filefmtchkpt3:

	//write disk 3-4
	for(n = 2;n<rfosdisk.numdisk;n++){
		fseeko64(disk[n],0,SEEK_SET);
		fwrite(&vblock[1],vblock[1].blocksize,1,disk[n]);
		fwrite(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[n]);
	}

		fseeko64(disk[1],0,SEEK_SET);
		fseeko64(disk[2],0,SEEK_SET);
		fread(&vblock[0],sizeof(VCB),1,disk[1]);
		fread(&vblock[1],sizeof(VCB),1,disk[2]);
		goto readdatapt;
	}
	else if(pass == 4){				
		// wrong placement of disk0
		//this will swap rfosdisk.disk properties
		// and relay data with newly formatted disk.
		fclose(disk[2]);
		fclose(disk[0]);
		disk[0] = fopen64(rfosdisk.disk[2],"rb+");
		disk[2] = fopen64(rfosdisk.disk[0],"rb+");
		gchar* temp1 = g_strdup(rfosdisk.disk[0]);
		g_free(rfosdisk.disk[0]);
		rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
		g_free(rfosdisk.disk[2]);
		rfosdisk.disk[2] = g_strdup(temp1);
		g_free(temp1);
		goto passonechkpt;
	}
	else if(pass == 8){
		//similar to above
		fclose(disk[3]);
		fclose(disk[0]);
		disk[0] = fopen64(rfosdisk.disk[3],"rb+");
		disk[3] = fopen64(rfosdisk.disk[0],"rb+");
		gchar* temp1 = g_strdup(rfosdisk.disk[0]);
		g_free(rfosdisk.disk[0]);
		rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
		g_free(rfosdisk.disk[3]);
		rfosdisk.disk[3] = g_strdup(temp1);
		g_free(temp1);
		goto passonechkpt;
	}
	else if(pass == 3 && rfosdisk.numdisk > 2){
		//format last 2 disk and continue initialized RFOS
		goto passtwochkpt;
	}
	else if(pass == 6){ //0110
		//Wrong placement of disk0-1
		//Rearrange and format last 1-2 disk(s) and
		// continue initialized RFOS
		fclose(disk[2]);
		fclose(disk[0]);
		disk[0] = fopen64(rfosdisk.disk[2],"rb+");
		disk[2] = fopen64(rfosdisk.disk[0],"rb+");
		gchar* temp1 = g_strdup(rfosdisk.disk[0]);
		g_free(rfosdisk.disk[0]);
		rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
		g_free(rfosdisk.disk[2]);
		rfosdisk.disk[2] = g_strdup(temp1);
		g_free(temp1);
		goto passtwochkpt;
	}
	else if(pass == 12){//1100
		//Wrong placement of disk0-1
		//Rearrange and format last 2 disks and
		// continue initialized RFOS
		fclose(disk[3]);
		fclose(disk[0]);
		disk[0] = fopen64(rfosdisk.disk[3],"rb+");
		disk[3] = fopen64(rfosdisk.disk[0],"rb+");
		gchar* temp1 = g_strdup(rfosdisk.disk[0]);
		g_free(rfosdisk.disk[0]);
		rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
		g_free(rfosdisk.disk[3]);
		rfosdisk.disk[3] = g_strdup(temp1);
		g_free(temp1);
		
		fclose(disk[2]);
		fclose(disk[1]);
		disk[1] = fopen64(rfosdisk.disk[2],"rb+");
		disk[2] = fopen64(rfosdisk.disk[1],"rb+");
		temp1 = g_strdup(rfosdisk.disk[1]);
		g_free(rfosdisk.disk[1]);
		rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
		g_free(rfosdisk.disk[2]);
		rfosdisk.disk[2] = g_strdup(temp1);
		g_free(temp1);
		goto passtwochkpt;
	}
	else if(pass == 5){ // 0101
		//Wrong placement of disk0-1
		//Rearrange and format last 1-2 disk(s) and
		// continue initialized RFOS
		fclose(disk[2]);
		fclose(disk[1]);
		disk[1] = fopen64(rfosdisk.disk[2],"rb+");
		disk[2] = fopen64(rfosdisk.disk[1],"rb+");
		gchar *temp1 = g_strdup(rfosdisk.disk[1]);
		g_free(rfosdisk.disk[1]);
		rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
		g_free(rfosdisk.disk[2]);
		rfosdisk.disk[2] = g_strdup(temp1);
		g_free(temp1);
		goto passtwochkpt;
	}
	else if(pass == 9){//1001
		//Wrong placement of disk0-1
		//Rearrange and format last 2 disks and
		// continue initialized RFOS
		fclose(disk[3]);
		fclose(disk[1]);
		disk[1] = fopen64(rfosdisk.disk[3],"rb+");
		disk[3] = fopen64(rfosdisk.disk[1],"rb+");
		gchar* temp1 = g_strdup(rfosdisk.disk[1]);
		g_free(rfosdisk.disk[1]);
		rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
		g_free(rfosdisk.disk[3]);
		rfosdisk.disk[3] = g_strdup(temp1);
		g_free(temp1);
		goto passtwochkpt;
	}
	else if(pass == 10){//1010
		//Wrong placement of disk0-1
		//Rearrange and format last 2 disks and
		// continue initialized RFOS
		fclose(disk[3]);
		fclose(disk[0]);
		disk[0] = fopen64(rfosdisk.disk[3],"rb+");
		disk[3] = fopen64(rfosdisk.disk[0],"rb+");
		gchar* temp1 = g_strdup(rfosdisk.disk[0]);
		g_free(rfosdisk.disk[0]);
		rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
		g_free(rfosdisk.disk[3]);
		rfosdisk.disk[3] = g_strdup(temp1);
		g_free(temp1);
		goto passtwochkpt;
	}
	else if(pass == 7){ // 0111
		//Case first 2 disk OK(or last 2 disk OK) but first/last is not
		//Check for alignment and relay data (if have)
		//and continue initialized RFOS
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[1],0,SEEK_SET);
		fseeko64(disk[2],0,SEEK_SET);
		fread(&tempvcb[0],sizeof(VCB),1,disk[0]);
		fread(&tempvcb[1],sizeof(VCB),1,disk[1]);
		fread(&tempvcb[2],sizeof(VCB),1,disk[2]);
		if(tempvcb[0].diskno == 0 && tempvcb[1].diskno == 0 && rfosdisk.numdisk==4){
			//relay data from disk 2 to disk 3 and continue initialized RFOS
			missdisk23chkpt:
			g_printf("Relay data from disk2 to disk3...\n");
			stat64(rfosdisk.disk[2],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[2]);
				if(!feof(disk[2])){
					fwrite(buf,1,buffersize,disk[3]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[3]);
					break;
				}
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 0 && tempvcb[2].diskno == 0){ //0111
			//swap data from disk 2 to disk 1 and			
			//relay data from disk 2 to disk 3 and continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			pass7prerdpt:
			if(rfosdisk.numdisk==4)goto missdisk23chkpt;
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[1].diskno == 0 && tempvcb[2].diskno == 0){
			//swap data from disk 0 to disk 2 and			
			//relay data from disk 2 to disk 3 and continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			goto pass7prerdpt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[1].diskno == 1){
			//Wrong placement of disk0 and 1 which are disk2-3
			//swap them to desire place and relay data from disk 0 to disk 1(if have)
			//and continue to initialized RFOS **disk1 have no data ->swap data from disk0 to disk1
			if(rfosdisk.numdisk==4){
				fclose(disk[3]);
				fclose(disk[1]);
				disk[1] = fopen64(rfosdisk.disk[3],"rb+");
				disk[3] = fopen64(rfosdisk.disk[1],"rb+");
				gchar* temp1 = g_strdup(rfosdisk.disk[1]);
				g_free(rfosdisk.disk[1]);
				rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
				g_free(rfosdisk.disk[3]);
				rfosdisk.disk[3] = g_strdup(temp1);
				g_free(temp1);
				fclose(disk[2]);
				fclose(disk[0]);
				disk[0] = fopen64(rfosdisk.disk[2],"rb+");
				disk[2] = fopen64(rfosdisk.disk[0],"rb+");
				temp1 = g_strdup(rfosdisk.disk[0]);
				g_free(rfosdisk.disk[0]);
				rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
				g_free(rfosdisk.disk[2]);
				rfosdisk.disk[2] = g_strdup(temp1);
				g_free(temp1);
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
				fseeko64(disk[2],0,SEEK_SET);
				fread(&vblock[0],sizeof(VCB),1,disk[0]);
				fread(&vblock[1],sizeof(VCB),1,disk[2]);
				goto readdatapt;
			}//end if
			else{ //0111 rfosdisk.numdisk = 3
			//swap disk 0 to 2 and continue to initialized RFOS....
				fclose(disk[2]);
				fclose(disk[0]);
				disk[0] = fopen64(rfosdisk.disk[2],"rb+");
				disk[2] = fopen64(rfosdisk.disk[0],"rb+");
				gchar* temp1 = g_strdup(rfosdisk.disk[0]);
				g_free(rfosdisk.disk[0]);
				rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
				g_free(rfosdisk.disk[2]);
				rfosdisk.disk[2] = g_strdup(temp1);
				g_free(temp1);
				fseeko64(disk[0],0,SEEK_SET);
				fseeko64(disk[1],0,SEEK_SET);
				fread(&vblock[0],sizeof(VCB),1,disk[0]);
				fread(&vblock[1],sizeof(VCB),1,disk[1]);
				goto readdatapt;
			}
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[2].diskno == 1){
			if(rfosdisk.numdisk==4){
			//swap disk 0 and disk 3
			//relay data from disk1 to disk 0
			//and continue initialized RFOS
				fclose(disk[3]);
				fclose(disk[0]);
				disk[0] = fopen64(rfosdisk.disk[3],"rb+");
				disk[3] = fopen64(rfosdisk.disk[0],"rb+");
				gchar* temp1 = g_strdup(rfosdisk.disk[0]);
				g_free(rfosdisk.disk[0]);
				rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
				g_free(rfosdisk.disk[3]);
				rfosdisk.disk[3] = g_strdup(temp1);
				g_free(temp1);
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
				fseeko64(disk[0],0,SEEK_SET);
				fseeko64(disk[2],0,SEEK_SET);
				fread(&vblock[0],sizeof(VCB),1,disk[0]);
				fread(&vblock[1],sizeof(VCB),1,disk[2]);
				goto readdatapt;
			}
			else{ 
				//swap disk 0 and disk 1 and continue initialized RFOS
				fclose(disk[1]);
				fclose(disk[0]);
				disk[0] = fopen64(rfosdisk.disk[1],"rb+");
				disk[1] = fopen64(rfosdisk.disk[0],"rb+");
				gchar* temp1 = g_strdup(rfosdisk.disk[0]);
				g_free(rfosdisk.disk[0]);
				rfosdisk.disk[0] = g_strdup(rfosdisk.disk[1]);
				g_free(rfosdisk.disk[1]);
				rfosdisk.disk[1] = g_strdup(temp1);
				g_free(temp1);
				fseeko64(disk[0],0,SEEK_SET);
				fseeko64(disk[1],0,SEEK_SET);
				fread(&vblock[0],sizeof(VCB),1,disk[0]);
				fread(&vblock[1],sizeof(VCB),1,disk[1]);
				goto readdatapt;
			}
		}
		else if(tempvcb[1].diskno == 1 && tempvcb[2].diskno == 1){//0111
			if(rfosdisk.numdisk==4){
			//move disk2 to disk3 and move disk 1 to disk 2
			//relay data from disk0 to disk1
			//continue initialized RFOS
				fclose(disk[3]);
				fclose(disk[2]);
				disk[2] = fopen64(rfosdisk.disk[3],"rb+");
				disk[3] = fopen64(rfosdisk.disk[2],"rb+");
				gchar* temp1 = g_strdup(rfosdisk.disk[2]);
				g_free(rfosdisk.disk[2]);
				rfosdisk.disk[2] = g_strdup(rfosdisk.disk[3]);
				g_free(rfosdisk.disk[3]);
				rfosdisk.disk[3] = g_strdup(temp1);
				g_free(temp1);
				fclose(disk[2]);
				fclose(disk[1]);
				disk[1] = fopen64(rfosdisk.disk[2],"rb+");
				disk[2] = fopen64(rfosdisk.disk[1],"rb+");
				temp1 = g_strdup(rfosdisk.disk[1]);
				g_free(rfosdisk.disk[1]);
				rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
				g_free(rfosdisk.disk[2]);
				rfosdisk.disk[2] = g_strdup(temp1);
				g_free(temp1);
				
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
				fseeko64(disk[2],0,SEEK_SET);
				fread(&vblock[0],sizeof(VCB),1,disk[0]);
				fread(&vblock[1],sizeof(VCB),1,disk[2]);
				goto readdatapt;			
			}
			else{
				fseeko64(disk[0],0,SEEK_SET);
				fseeko64(disk[1],0,SEEK_SET);
				fread(&vblock[0],sizeof(VCB),1,disk[0]);
				fread(&vblock[1],sizeof(VCB),1,disk[1]);
				goto readdatapt;
			}
			//case disk1 disk 2 have complete data but disk 0(first half have uncomplete data....
		}
		else{exit(1);}
	}
	else if(pass == 11){ 
		//Case first 2 disk OK(or last 2 disk OK) but first/last is not
		//Check for alignment and relay data
		//and continue initialized RFOS
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[1],0,SEEK_SET);
		fseeko64(disk[3],0,SEEK_SET);
		fread(&tempvcb[0],sizeof(VCB),1,disk[0]);
		fread(&tempvcb[1],sizeof(VCB),1,disk[1]);
		fread(&tempvcb[3],sizeof(VCB),1,disk[3]);
		if(tempvcb[0].diskno == 0 && tempvcb[1].diskno == 0){
			//relay data from disk3 to disk2 and continue initialized RFOS
			pass11chkpt:						
			stat64(rfosdisk.disk[3],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[3]);
				if(!feof(disk[3])){
					fwrite(buf,1,buffersize,disk[2]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[2]);
					break;
				}			
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 0 && tempvcb[3].diskno == 0){
		//swap disk 3 and disk 1
		//relay data from disk 3 to disk 2 and coninue initialized RFOS
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			goto pass11chkpt;				
		}
		else if(tempvcb[1].diskno == 0 && tempvcb[3].diskno == 0){
		//swap disk 0 and disk 1
		//swap disk 3 and disk 1
		//relay data from disk3 to disk2 and continue initialized RFOS
			fclose(disk[1]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[1],"rb+");
			disk[1] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(temp1);
			g_free(temp1);
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			goto pass11chkpt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[1].diskno == 1){
			//swap disk 0 to disk 3
			//swap disk 1 to disk 2
			//relay data from disk 0 to disk 1 and continue initialized RFOS
			fclose(disk[3]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			
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
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[3].diskno == 1){ //1011
		//swap disk 0 to disk 2
		//relay data from disk 1 to disk 0
		//and continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			
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
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[1].diskno == 1 && tempvcb[3].diskno == 1){ //1011
			//swap disk 1 to disk 2
			//relay data from disk 0 to disk 1
			//continue to initialized RFOS
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			
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
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else{exit(1);}
	}
	else if(pass == 13){
		//Case first 2 disk OK(or last 2 disk OK) but first/last is not
		//Check for alignment and relay data
		//and continue initialized RFOS
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[2],0,SEEK_SET);
		fseeko64(disk[3],0,SEEK_SET);
		fread(&tempvcb[0],sizeof(VCB),1,disk[0]);
		fread(&tempvcb[2],sizeof(VCB),1,disk[2]);
		fread(&tempvcb[3],sizeof(VCB),1,disk[3]);
		if(tempvcb[0].diskno == 0 && tempvcb[2].diskno == 0){
		//swap disk 2 to disk 1
		//relay data from disk 3 to disk 2
		//continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
		
			stat64(rfosdisk.disk[3],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[3]);
				if(!feof(disk[3])){
					fwrite(buf,1,buffersize,disk[2]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[2]);
					break;
				}			
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 0 && tempvcb[3].diskno == 0){
			//swap disk 3 to disk 1
			//relay data from disk 2 to disk 3
			//continue initialized RFOS
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			
			stat64(rfosdisk.disk[2],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[2]);
				if(!feof(disk[2])){
					fwrite(buf,1,buffersize,disk[3]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[3]);
					break;
				}
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[2].diskno == 0 && tempvcb[3].diskno == 0){
			//swap disk 2 - disk 1
			//swap disk 3 - disk 0
			//relay data from disk 3 to disk 2 and continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			fclose(disk[3]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[0],"rb+");
			temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			
			stat64(rfosdisk.disk[3],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[3]);
				if(!feof(disk[3])){
					fwrite(buf,1,buffersize,disk[2]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[2]);
					break;
				}			
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[2].diskno == 1){//1101   023
		//swap disk 3 - disk 0
		//relay data from disk 0 to disk 1
		//and continue initialized RFOS
			fclose(disk[3]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[0],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			pass13chkpt:
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
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[3].diskno == 1){
		//swap disk 0 - disk 2
		//relay data from disk 0 to disk 1
		//continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			goto pass13chkpt;
		}
		else if(tempvcb[2].diskno == 1 && tempvcb[3].diskno == 1){//1101   023
			//relay data from disk 0 to disk 1 and continue initialized RFOS
			goto pass13chkpt;
		}
		else{exit(1);}
	}
	else if(pass == 14){
		//Case first 2 disk OK(or last 2 disk OK) but first/last is not
		//Check for alignment and relay data
		//and continue initialized RFOS
		fseeko64(disk[1],0,SEEK_SET);
		fseeko64(disk[2],0,SEEK_SET);
		fseeko64(disk[3],0,SEEK_SET);
		fread(&tempvcb[1],sizeof(VCB),1,disk[1]);
		fread(&tempvcb[2],sizeof(VCB),1,disk[2]);
		fread(&tempvcb[3],sizeof(VCB),1,disk[3]);
		if(tempvcb[1].diskno == 0 && tempvcb[2].diskno == 0){
		//swap disk 2 - disk 0
		//relay data from disk 3 to disk 2
		//continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			
			stat64(rfosdisk.disk[3],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[3]);
				if(!feof(disk[3])){
					fwrite(buf,1,buffersize,disk[2]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[2]);
					break;
				}			
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[1].diskno == 0 && tempvcb[3].diskno == 0){
		//swap disk 3 - disk 0
		//relay data from disk 2 to disk 3
		//continue initialized RFOS
			fclose(disk[3]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[0],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			
			stat64(rfosdisk.disk[2],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[2]);
				if(!feof(disk[2])){
					fwrite(buf,1,buffersize,disk[3]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[3]);
					break;
				}
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[2].diskno == 0 && tempvcb[3].diskno == 0){ 
			//swap disk 2 - disk 0
			//swap disk 1 - disk 3
			//relay data from disk 3 to disk 2 and continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			
			stat64(rfosdisk.disk[3],&s);
			fseeko64(disk[2],0,SEEK_SET);
			fseeko64(disk[3],0,SEEK_SET);
			while(TRUE){
				fread(buf,1,buffersize,disk[3]);
				if(!feof(disk[3])){
					fwrite(buf,1,buffersize,disk[2]);
				}
				else{
					if(s.st_size % buffersize != 0)
						fwrite(buf,1,s.st_size % buffersize,disk[2]);
					break;
				}			
			}
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[1].diskno == 1 && tempvcb[2].diskno == 1){ 
			//swap disk 1 - disk 3
			//relay data from disk 1 to disk 0
			//continue initialized RFOS
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			
			pass14chkpt:
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
			fseeko64(disk[0],0,SEEK_SET);
			fseeko64(disk[2],0,SEEK_SET);
			fread(&vblock[0],sizeof(VCB),1,disk[0]);
			fread(&vblock[1],sizeof(VCB),1,disk[2]);
			goto readdatapt;
		}
		else if(tempvcb[1].diskno == 1 && tempvcb[3].diskno == 1){
		//swap disk 1 - disk 2
		//relay data from disk 1 to disk 0 and continue initialized RFOS
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			goto pass14chkpt;
		}
		else if(tempvcb[2].diskno == 1 && tempvcb[3].diskno == 1){ //1110 123
			//relay data from disk 1 to disk 0 and continue initialized RFOS
			goto pass14chkpt;	
		}
		else{exit(1);}
	}
	else if(pass == 15){
		fseeko64(disk[0],0,SEEK_SET);
		fseeko64(disk[1],0,SEEK_SET);
		fseeko64(disk[2],0,SEEK_SET);
		fseeko64(disk[3],0,SEEK_SET);
		fread(&tempvcb[0],sizeof(VCB),1,disk[0]);
		fread(&tempvcb[1],sizeof(VCB),1,disk[1]);
		fread(&tempvcb[2],sizeof(VCB),1,disk[2]);
		fread(&tempvcb[3],sizeof(VCB),1,disk[3]);
		if(tempvcb[0].diskno == 0 && tempvcb[1].diskno == 0){
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[1].diskno == 1 && tempvcb[2].diskno == 0 && tempvcb[3].diskno == 0){
		//swap disk 0 to disk 2
		//swap disk 1 to disk 3
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[1].diskno == 0 && tempvcb[2].diskno == 1 && tempvcb[3].diskno == 0){
		//swap disk 0 - disk 3
			fclose(disk[3]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[0],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 0 && tempvcb[1].diskno == 1 && tempvcb[2].diskno == 1 && tempvcb[3].diskno == 0){
			//swap disk 1 - disk 3
			fclose(disk[3]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[3],"rb+");
			disk[3] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[3]);
			g_free(rfosdisk.disk[3]);
			rfosdisk.disk[3] = g_strdup(temp1);
			g_free(temp1);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 1 && tempvcb[1].diskno == 0 && tempvcb[2].diskno == 0 && tempvcb[3].diskno == 1){
			//swap disk0 - disk 2
			fclose(disk[2]);
			fclose(disk[0]);
			disk[0] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[0],"rb+");
			gchar* temp1 = g_strdup(rfosdisk.disk[0]);
			g_free(rfosdisk.disk[0]);
			rfosdisk.disk[0] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			goto readdatapt;
		}
		else if(tempvcb[0].diskno == 0 && tempvcb[1].diskno == 1 && tempvcb[2].diskno == 0 && tempvcb[3].diskno == 1){
			//swap disk 1 - disk 2
			fclose(disk[2]);
			fclose(disk[1]);
			disk[1] = fopen64(rfosdisk.disk[2],"rb+");
			disk[2] = fopen64(rfosdisk.disk[1],"rb+");
			gchar *temp1 = g_strdup(rfosdisk.disk[1]);
			g_free(rfosdisk.disk[1]);
			rfosdisk.disk[1] = g_strdup(rfosdisk.disk[2]);
			g_free(rfosdisk.disk[2]);
			rfosdisk.disk[2] = g_strdup(temp1);
			g_free(temp1);
			goto readdatapt;		
		}
		else{exit(1);}
	}
	else{ //1 disk pass,2 disk pass,3disk pass,4disk pass 
	      //if all disks are success to read,default is read data from disk0
              // if disk 2-3 are add read it also.
	readdatapt:
	g_printf("FILE SYSTEM DETAIL....\n");
	g_printf("DISK NO:%d\n",vblock[0].diskno);
	g_printf("BLOCK SIZE:%d\n",vblock[0].blocksize);
	g_printf("NUMBLOCK:%d\n",vblock[0].numblock);
	g_printf("BIT VECTOR BLOCK COUNT:%d\n",vblock[0].bitvec_bl);
	g_printf("FILE ENTRY BLOCK COUNT:%d\n",vblock[0].file_ent_bl);
	g_printf("FREE:%d\n",vblock[0].free);
	g_printf("FILE COUNT:%d\n",vblock[0].numfile);
	g_printf("INVALID FILE:%d\n",vblock[0].inv_file);
	bitvec[0] = g_new(gchar,vblock[0].bitvec_bl*vblock[0].blocksize);
	fseeko64(disk[0],vblock[0].blocksize,SEEK_SET);
	fread(bitvec[0],1,vblock[0].bitvec_bl*vblock[0].blocksize,disk[0]);
	j=0;
	guint32 bituse=0;
	for(i = 0;;i++){
		for(k=0;k<8;k++){
			if(j>=vblock[0].numblock)goto filerdrchkpt;
			if((bitvec[0][i] & bytechk[k]) == bytechk[k])bituse++;
			j++;
		}
	}
	filerdrchkpt:
	g_printf("NUM OF BLOCK USED:%d\n",bituse);
	FILE_ENT fe;
	FILE_ENT_INMEM *entry;
	g_printf("READ FILES:%d\n",vblock[0].numfile);
	guint feoff = vblock[0].bitvec_bl+1;
	fseeko64(disk[0],feoff*vblock[0].blocksize,SEEK_SET);
	for(i=0;i<vblock[0].numfile;i++){
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
	//read disk2-3 if have
	if(rfosdisk.numdisk>2){
		g_printf("DISK NO:%d\n",vblock[1].diskno);
		g_printf("BLOCK SIZE:%d\n",vblock[1].blocksize);
		g_printf("NUMBLOCK:%d\n",vblock[1].numblock);
		g_printf("BIT VECTOR BLOCK COUNT:%d\n",vblock[1].bitvec_bl);
		g_printf("FREE:%d\n",vblock[1].free);
		bitvec[1] = g_new(gchar,vblock[1].bitvec_bl*vblock[1].blocksize);
		fseeko64(disk[2],vblock[1].blocksize,SEEK_SET);
		fread(bitvec[1],1,vblock[1].bitvec_bl*vblock[1].blocksize,disk[2]);
		j=0;bituse=0;
		for(i = 0;;i++){
			for(k=0;k<8;k++){
				if(j>=vblock[1].numblock)goto filerdrchkpt1;
				if((bitvec[1][i] & bytechk[k]) == bytechk[k])bituse++;
				j++;
			}
		}
	filerdrchkpt1:
	g_printf("NUM OF BLOCK USED:%d\n",bituse);
	}
}

for(n = 0;n <rfosdisk.numdisk;n++)
fclose(disk[n]);
desiresize = vblock[0].blocksize-4;
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
