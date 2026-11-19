// SPDX-License-Identifier: GPL-3.0-or-later
#include "np-ap.h"

void np_network_free(NpNetwork *n)
{
    if (!n) return;
    g_bytes_unref(n->ssid);
    g_free(n->name);
    g_clear_object(&n->ap);
    g_clear_object(&n->device);
    g_clear_object(&n->connection);
    g_free(n);
}

void np_network_set_connection(NpNetwork *n, gpointer connection)
{
    g_set_object(&n->connection, connection);
}

NpNetwork *np_networks_find(GPtrArray *nets, GBytes *ssid)
{
    if (!ssid) return NULL;
    for (guint i = 0; i < nets->len; i++) {
        NpNetwork *n = nets->pdata[i];
        if (g_bytes_equal(n->ssid, ssid)) return n;
    }
    return NULL;
}

GPtrArray *np_networks_from_aps(const NpApInput *aps, gsize n_aps)
{
    GPtrArray *nets = g_ptr_array_new_with_free_func((GDestroyNotify)np_network_free);
    GHashTable *by_ssid = g_hash_table_new(g_bytes_hash, g_bytes_equal);
    GHashTable *pinned = g_hash_table_new(NULL, NULL); /* 已被“当前 AP”占用的网络 */

    for (gsize i = 0; i < n_aps; i++) {
        const NpApInput *in = &aps[i];
        if (!in->ssid || !g_bytes_get_size(in->ssid) || in->mode != NM_802_11_MODE_INFRA) continue;

        NpNetwork *n = g_hash_table_lookup(by_ssid, in->ssid);
        if (!n) {
            n = g_new0(NpNetwork, 1);
            n->ssid = g_bytes_ref(in->ssid);
            n->name = np_ssid_to_display(in->ssid);
            g_hash_table_insert(by_ssid, n->ssid, n);
            g_ptr_array_add(nets, n);
        } else if (g_hash_table_contains(pinned, n) || (!in->is_active && in->strength <= n->strength)) {
            continue;
        }
        if (in->is_active) g_hash_table_add(pinned, n);
        n->strength = in->strength;
        n->frequency = in->frequency;
        n->security = np_ap_security_classify(in->flags, in->wpa_flags, in->rsn_flags);
        g_set_object(&n->ap, in->ap);
        g_set_object(&n->device, in->device);
    }
    g_hash_table_destroy(pinned);
    g_hash_table_destroy(by_ssid);
    return nets;
}

static int rank(const NpNetwork *n)
{
    return n->active ? 0 : n->activating ? 1 : n->connection ? 2 : 3;
}

gint np_network_compare(const NpNetwork *a, const NpNetwork *b)
{
    int r = rank(a) - rank(b);
    if (r) return r;
    if (a->strength != b->strength) return (int)b->strength - (int)a->strength;
    return g_utf8_collate(a->name, b->name);
}

static gint cmp_ptr(gconstpointer a, gconstpointer b)
{
    return np_network_compare(*(NpNetwork *const *)a, *(NpNetwork *const *)b);
}

void np_networks_sort(GPtrArray *nets)
{
    g_ptr_array_sort(nets, cmp_ptr);
}
