// SPDX-License-Identifier: GPL-3.0-or-later
#include "np-ap.h"

static NpApInput ap(const char *ssid, guint8 strength, guint32 rsn, gboolean active)
{
    NpApInput a = { 0 };
    a.ssid = ssid ? g_bytes_new_static(ssid, strlen(ssid)) : NULL;
    a.strength = strength;
    a.frequency = 2437;
    a.flags = rsn ? NM_802_11_AP_FLAGS_PRIVACY : 0;
    a.rsn_flags = rsn;
    a.mode = NM_802_11_MODE_INFRA;
    a.is_active = active;
    return a;
}

static void free_inputs(NpApInput *in, gsize n)
{
    for (gsize i = 0; i < n; i++)
        if (in[i].ssid) g_bytes_unref(in[i].ssid);
}

static NpNetwork *find(GPtrArray *nets, const char *name)
{
    for (guint i = 0; i < nets->len; i++) {
        NpNetwork *n = nets->pdata[i];
        if (!g_strcmp0(n->name, name)) return n;
    }
    return NULL;
}

static void test_group_strongest(void)
{
    NpApInput in[] = {
        ap("home", 40, NM_802_11_AP_SEC_KEY_MGMT_PSK, FALSE),
        ap("home", 80, NM_802_11_AP_SEC_KEY_MGMT_PSK, FALSE),
        ap("home", 60, NM_802_11_AP_SEC_KEY_MGMT_PSK, FALSE),
        ap("cafe", 30, 0, FALSE),
    };
    GPtrArray *nets = np_networks_from_aps(in, G_N_ELEMENTS(in));
    g_assert_cmpuint(nets->len, ==, 2);
    g_assert_cmpuint(find(nets, "home")->strength, ==, 80);
    g_assert_cmpint(find(nets, "home")->security, ==, NP_AP_SEC_WPA2);
    g_assert_cmpint(find(nets, "cafe")->security, ==, NP_AP_SEC_OPEN);
    g_ptr_array_unref(nets);
    free_inputs(in, G_N_ELEMENTS(in));
}

static void test_group_active_pinned(void)
{
    /* 当前连接的 AP 即使信号较弱也保留，避免列表显示“错误”的 BSS */
    NpApInput in[] = {
        ap("home", 90, NM_802_11_AP_SEC_KEY_MGMT_PSK, FALSE),
        ap("home", 50, NM_802_11_AP_SEC_KEY_MGMT_PSK, TRUE),
        ap("home", 95, NM_802_11_AP_SEC_KEY_MGMT_PSK, FALSE),
    };
    GPtrArray *nets = np_networks_from_aps(in, G_N_ELEMENTS(in));
    g_assert_cmpuint(nets->len, ==, 1);
    g_assert_cmpuint(find(nets, "home")->strength, ==, 50);
    g_ptr_array_unref(nets);
    free_inputs(in, G_N_ELEMENTS(in));
}

static void test_skip_hidden_and_adhoc(void)
{
    NpApInput in[] = { ap(NULL, 90, 0, FALSE), ap("", 90, 0, FALSE), ap("adhoc", 90, 0, FALSE), ap("ok", 10, 0, FALSE) };
    in[2].mode = NM_802_11_MODE_ADHOC;
    GPtrArray *nets = np_networks_from_aps(in, G_N_ELEMENTS(in));
    g_assert_cmpuint(nets->len, ==, 1);
    g_assert_nonnull(find(nets, "ok"));
    g_ptr_array_unref(nets);
    free_inputs(in, G_N_ELEMENTS(in));
}

static void test_sort(void)
{
    NpApInput in[] = {
        ap("weak", 20, 0, FALSE), ap("strong", 90, 0, FALSE), ap("saved", 30, 0, FALSE),
        ap("active", 10, 0, TRUE), ap("connecting", 15, 0, FALSE), ap("beta", 50, 0, FALSE), ap("alpha", 50, 0, FALSE),
    };
    GPtrArray *nets = np_networks_from_aps(in, G_N_ELEMENTS(in));
    GObject *dummy = g_object_new(G_TYPE_OBJECT, NULL);
    np_network_set_connection(find(nets, "saved"), dummy);
    find(nets, "active")->active = TRUE;
    find(nets, "connecting")->activating = TRUE;
    np_networks_sort(nets);

    const char *order[] = { "active", "connecting", "saved", "strong", "alpha", "beta", "weak" };
    g_assert_cmpuint(nets->len, ==, G_N_ELEMENTS(order));
    for (guint i = 0; i < nets->len; i++)
        g_assert_cmpstr(((NpNetwork *)nets->pdata[i])->name, ==, order[i]);

    GBytes *k = g_bytes_new_static("beta", 4);
    g_assert_true(np_networks_find(nets, k) == find(nets, "beta"));
    g_bytes_unref(k);
    g_assert_null(np_networks_find(nets, NULL));

    g_ptr_array_unref(nets); /* 释放网络时会 unref dummy */
    g_object_unref(dummy);
    free_inputs(in, G_N_ELEMENTS(in));
}

static void test_refs(void)
{
    GObject *apobj = g_object_new(G_TYPE_OBJECT, NULL);
    NpApInput in[] = { ap("x", 10, 0, FALSE) };
    in[0].ap = apobj;
    GPtrArray *nets = np_networks_from_aps(in, 1);
    g_assert_true(find(nets, "x")->ap == apobj);
    g_object_add_weak_pointer(apobj, (gpointer *)&apobj);
    g_object_unref(apobj);
    g_assert_nonnull(apobj); /* 网络持有引用 */
    g_ptr_array_unref(nets);
    g_assert_null(apobj);
    free_inputs(in, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ap/group-strongest", test_group_strongest);
    g_test_add_func("/ap/group-active-pinned", test_group_active_pinned);
    g_test_add_func("/ap/skip-hidden-adhoc", test_skip_hidden_and_adhoc);
    g_test_add_func("/ap/sort", test_sort);
    g_test_add_func("/ap/refs", test_refs);
    return g_test_run();
}
