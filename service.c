#include "rfos.h"
#include <glib/gprintf.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
int ready = 0;
pthread_mutex_t lock;

typedef struct _vcb{
guint32 numblock;
guint32 free;
guint32 diskno;
}VCB;

typedef struct _fe{
gchar key[8];
guint32 size;
guint64 atime;
guint32 sblock;
}FILE_ENT;

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
    guint64 test;
	for(test=1;test<3500000000;test++);
if(ready==0)err = EBUSY;
    else if(strlen(p->key)!=8)
	err = ENAMETOOLONG;
    else
    	err = 0;

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
FILE *s;
s = fopen("testwrite","w");
g_fprintf(s,"test1234");
fclose(s);
/*struct stat sta;
stat("disk8.img",&sta);
g_printf("SIZE = %ld\n",sta.st_size);*/
ready = 1;
pthread_exit(0);
}
int main (int argc,char **argv)
{
pthread_t test;
DISK d;
if(argc > 1){
d.numdisk = argc - 1;
	if(argc == 2){
	d.disk1 = argv[1];
	}
	else if(argc == 3){
	d.disk1 = argv[1];
	d.disk2 = argv[2];
	}
	else if(argc == 4){
	d.disk1 = argv[1];
	d.disk2 = argv[2];
	d.disk3 = argv[3];
	}
	else{
	d.disk1 = argv[1];
	d.disk2 = argv[2];
	d.disk3 = argv[3];
	d.disk4 = argv[4];
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
    /* Start the main loop */

//Initialized RFOS (FORMATTING) via another thread
pthread_create(&test,NULL,dummywork,&d);
    g_main_loop_run (loop);
}
else{
g_printf("Usage:./rfos-svc [disk1-name] [disk2-name] [disk3-name] [disk4-name]\n"); 
} 
   return 0;
}
