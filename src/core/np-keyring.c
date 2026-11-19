// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include "np-keyring.h"

#if HAVE_LIBSECRET
#include <libsecret/secret.h>

static const SecretSchema *schema(void)
{
    static const SecretSchema s = {
        NP_KEYRING_SCHEMA_NAME,
        SECRET_SCHEMA_DONT_MATCH_NAME,
        {
            { "connection-uuid", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "setting-name", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "setting-key", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { NULL, 0 },
        },
        0, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    };
    return &s;
}

gboolean np_keyring_available(void) { return TRUE; }

typedef struct {
    char *uuid;
    char *setting;
} Query;

static void query_free(Query *q)
{
    g_free(q->uuid);
    g_free(q->setting);
    g_free(q);
}

static void lookup_thread(GTask *task, gpointer src, gpointer data, GCancellable *cancel)
{
    Query *q = data;
    GError *e = NULL;
    GHashTable *attrs = secret_attributes_build(schema(), "connection-uuid", q->uuid, "setting-name", q->setting, NULL);
    GList *items = secret_password_searchv_sync(schema(), attrs,
                                                SECRET_SEARCH_ALL | SECRET_SEARCH_UNLOCK | SECRET_SEARCH_LOAD_SECRETS,
                                                cancel, &e);
    g_hash_table_unref(attrs);
    if (e) {
        g_task_return_error(task, e);
        return;
    }
    GHashTable *out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (GList *l = items; l; l = l->next) {
        SecretRetrievable *r = l->data;
        GHashTable *a = secret_retrievable_get_attributes(r);
        const char *key = g_hash_table_lookup(a, "setting-key");
        SecretValue *v = key ? secret_retrievable_retrieve_secret_sync(r, cancel, NULL) : NULL;
        if (v) {
            const char *text = secret_value_get_text(v);
            if (text) g_hash_table_replace(out, g_strdup(key), g_strdup(text));
            secret_value_unref(v);
        }
        g_hash_table_unref(a);
    }
    g_list_free_full(items, g_object_unref);
    g_task_return_pointer(task, out, (GDestroyNotify)g_hash_table_unref);
}

void np_keyring_lookup_async(const char *uuid, const char *setting_name, GCancellable *cancellable,
                             GAsyncReadyCallback cb, gpointer data)
{
    GTask *t = g_task_new(NULL, cancellable, cb, data);
    Query *q = g_new0(Query, 1);
    q->uuid = g_strdup(uuid);
    q->setting = g_strdup(setting_name);
    g_task_set_task_data(t, q, (GDestroyNotify)query_free);
    g_task_run_in_thread(t, lookup_thread);
    g_object_unref(t);
}

GHashTable *np_keyring_lookup_finish(GAsyncResult *res, GError **error)
{
    return g_task_propagate_pointer(G_TASK(res), error);
}

void np_keyring_store(const char *uuid, const char *id, const char *setting, const char *key, const char *value)
{
    char *label = g_strdup_printf("Network secret for %s/%s/%s", id ? id : uuid, setting, key);
    secret_password_store(schema(), SECRET_COLLECTION_DEFAULT, label, value, NULL, NULL, NULL,
                          "connection-uuid", uuid, "setting-name", setting, "setting-key", key, NULL);
    g_free(label);
}

void np_keyring_clear(const char *uuid)
{
    secret_password_clear(schema(), NULL, NULL, NULL, "connection-uuid", uuid, NULL);
}

#else /* !HAVE_LIBSECRET */

gboolean np_keyring_available(void) { return FALSE; }

void np_keyring_lookup_async(const char *uuid, const char *setting_name, GCancellable *cancellable,
                             GAsyncReadyCallback cb, gpointer data)
{
    GTask *t = g_task_new(NULL, cancellable, cb, data);
    g_task_return_pointer(t, g_hash_table_new(g_str_hash, g_str_equal), (GDestroyNotify)g_hash_table_unref);
    g_object_unref(t);
}

GHashTable *np_keyring_lookup_finish(GAsyncResult *res, GError **error)
{
    return g_task_propagate_pointer(G_TASK(res), error);
}

void np_keyring_store(const char *uuid, const char *id, const char *setting, const char *key, const char *value) {}
void np_keyring_clear(const char *uuid) {}

#endif
