#include "rfos.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

void print_doc(char *bin) {
    fprintf(stderr,"Usage: %s <command> [<args>]\n", bin);
    fprintf(stderr,"\tput <key> <src>\t\t\tAdd or replace an object identified by <key> using file <src>\n");
    fprintf(stderr,"\tget <key> <outpath>\t\tRetrieve the object identified by <key> and write it to <outpath>\n");
    fprintf(stderr,"\tremove <key>\t\t\tRemove the object identified by <key>\n");
    fprintf(stderr,"\tsearch <prefixkey> <outpath>\tSearch for objects whose key starts with <prefixkey> and write the result to <outpath>\n");
    fprintf(stderr,"\tstat <key>\t\t\tRetrieve the information of an object identified by <key>\n");
}

RFOS* get_proxy() {
    RFOS *fs_proxy = rfos_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        "kmitl.ce.os.RFOS",
        "/kmitl/ce/os/RFOS",
        NULL,
        NULL);
    g_dbus_proxy_set_default_timeout((GDBusProxy*)fs_proxy,600000);
    return fs_proxy;
}

int handle_error(guint err_code, GError *gerror) {
    if (gerror != NULL) {
        fprintf(stderr, "%s\n", gerror->message);
        return EINTR;
    }
    if (err_code) {
        fprintf(stderr, "%s\n", strerror(err_code));
    }
    return err_code;
}

int do_get(const char *key, const char *outpath) {
    RFOS *fs_proxy = get_proxy();
    guint err_code = 0;
    GError *gerror = NULL;
    rfos_call_get_sync(
        fs_proxy,
        key,
        outpath,
        &err_code,
        NULL,
        &gerror);

    return handle_error(err_code, gerror);
}

int do_put(const char *key, const char *src) {
    RFOS *fs_proxy = get_proxy();
    guint err_code = 0;
    GError *gerror = NULL;
    rfos_call_put_sync(
        fs_proxy,
        key,
        src,
        &err_code,
        NULL,
        &gerror);

    return handle_error(err_code, gerror);
}

int do_remove(const char *key) {
    RFOS *fs_proxy = get_proxy();
    guint err_code = 0;
    GError *gerror = NULL;
    rfos_call_remove_sync(
        fs_proxy,
        key,
        &err_code,
        NULL,
        &gerror);
    return handle_error(err_code, gerror);
}

int do_search(const char *key, const char *outpath) {
    RFOS *fs_proxy = get_proxy();
    guint err_code = 0;
    GError *gerror = NULL;
    rfos_call_search_sync(
        fs_proxy,
        key,
        outpath,
        &err_code,
        NULL,
        &gerror);

    return handle_error(err_code, gerror);
}

int do_stat(const char *key) {
    RFOS *fs_proxy = get_proxy();
    guint err_code = 0;
    GError *gerror = NULL;
    
    guint obj_size = 0;
    gint64 atime = -1;
    
    rfos_call_stat_sync(
        fs_proxy,
        key,
        &obj_size,
        &atime,
        &err_code,
        NULL,
        &gerror);
    err_code = handle_error(err_code, gerror);

    if (err_code) {
        return err_code;
    }

    fprintf(stdout, "%s %u %s", key, obj_size, ctime(&atime));

    return 0;
}

int main (int argc, char *argv[]) {
    if (argc < 2) {
        print_doc(argv[0]);
        return EINVAL;
    }

    if (strncmp(argv[1],"put",3) == 0) {
        if (argc < 4) {
            print_doc(argv[0]);
            return EINVAL;
        }
        const char *key = argv[2];
        const char *src = argv[3];
        return do_put(key, src);
    } else if (strncmp(argv[1], "get", 3) == 0) {
        if (argc < 4) {
            print_doc(argv[0]);
            return EINVAL;
        }
        const char *key = argv[2];
        const char *outpath = argv[3];
        return do_get(key, outpath);
    } else if (strncmp(argv[1], "remove", 6) == 0) {
        if (argc < 3) {
            print_doc(argv[0]);
            return EINVAL;
        }
        const char *key = argv[2];
        return do_remove(key);
    } else if (strncmp(argv[1], "search", 6) == 0) {
        if (argc < 4) {
            print_doc(argv[0]);
            return EINVAL;
        }
        const char *key = argv[2];
        const char *outpath = argv[3];
        return do_search(key, outpath);
    } else if (strncmp(argv[1], "stat", 4) == 0) {
        if (argc < 3) {
            print_doc(argv[0]);
            return EINVAL;
        }
        const char *key = argv[2];
        return do_stat(key);
    } else {
        print_doc(argv[0]);
        return EINVAL;
    }

    return 0;
}
