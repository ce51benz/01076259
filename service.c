#include "rfos.h"
#include <glib/gprintf.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
int ready = 0;
pthread_mutex_t lock;
typedef struct handle_getput_param{
RFOS *obj;
GDBusMethodInvocation *inv;
gchar *key;
gchar *path;
}getput_param;

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
g_free(pa);
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

    /** End of Get method execution, returning values **/
    return TRUE;
}

static gboolean on_handle_put (
    RFOS *object,
    GDBusMethodInvocation *invocation,
    const gchar *key,
    const gchar *src) {
    guint err;
    guint64 test;
	/*
    if(strlen(key)!=8)
	err = ENAMETOOLONG;
    else*/
	for(test=1;test<3500000000;test++);
    	err = 0;
    /** End of Put method execution, returning values **/
    rfos_complete_put(object, invocation, err);
 
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
    /* Export the RFOS service on the connection as /kmitl/ce/os/RFOS object  */
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
        connection,
        "/kmitl/ce/os/RFOS",
        NULL);
}

void *dummywork(void *t){
guint64 i;
for(i=0;i<4000000000;i++);
for(i=0;i<4000000000;i++);
ready = 1;
pthread_exit(0);
}
int main (void)
{
pthread_t test;
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
pthread_create(&test,NULL,dummywork,NULL);
    g_main_loop_run (loop);
    return 0;
}
