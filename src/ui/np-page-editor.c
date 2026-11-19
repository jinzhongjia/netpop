// SPDX-License-Identifier: GPL-3.0-or-later
/* 连接编辑器：表单 ⇄ NpConnModel ⇄ NMConnection
 *
 * 打开已有连接时先异步取回密钥（Wi-Fi / 802.1X / WireGuard），再构建表单，
 * 因此密码框里显示的是当前值；VPN 密钥不回显，只在填写时覆盖。
 */
#include "np-actions.h"
#include "np-app.h"
#include "np-pages.h"
#include "np-widgets.h"
#include "np-wg.h"

typedef struct {
    GtkWidget *card;
    GtkEntry *pub, *endpoint, *allowed, *psk;
    GtkSpinButton *keepalive;
} PeerForm;

typedef struct {
    GtkDropDown *method, *privacy;
    GtkWidget *fields;
    GtkTextView *addresses, *routes;
    GtkEntry *gateway, *dns, *search;
    GtkSwitch *never_default, *ignore_auto_dns;
} IpForm;

static const NpChoice mode_choices[] = { { "infrastructure", "客户端" }, { "ap", "热点 (AP)" } };

static struct {
    guint gen;
    NMRemoteConnection *rc; /* 编辑已有连接时 */
    NMConnection *base;     /* 工作副本（含取回的密钥）或导入的连接；模板新建时为 NULL */
    NpConnModel *model;
    gboolean activate_after;
    gboolean from_new_page;
    int pending;

    GtkLabel *title, *err;
    GtkWidget *holder, *save;
    GArray *iface_choices; /* NpChoice，字符串由本模块分配 */

    struct { GtkEntry *id, *iface_entry; GtkDropDown *iface; GtkSwitch *autoconnect, *all_users; GtkSpinButton *priority; GtkDropDown *metered; } g;
    struct { GtkEntry *ssid; GtkDropDown *mode, *band, *mac; GtkSwitch *hidden; GtkSpinButton *mtu; } w;
    struct {
        GtkDropDown *type, *eap, *phase2;
        GtkWidget *psk_box, *wep_box, *eap_box;
        GtkEntry *psk, *wep, *identity, *anon, *password, *ca, *domain, *cert, *key, *key_pw;
        GtkSwitch *system_ca;
    } s;
    struct { GtkDropDown *mac; GtkSpinButton *mtu; } e;
    struct { GtkEntry *priv, *port; GtkSpinButton *mtu; GtkLabel *pub; GtkWidget *peers_box; GPtrArray *peers; } wg;
    struct { GtkEntry *service, *user; GtkTextView *data, *secrets; } v;
    IpForm ip4, ip6;
    struct { GtkSwitch *on; GtkEntry *pac; } px;
} E;

static gboolean is_type(const char *t)
{
    return E.model && !g_strcmp0(E.model->type, t);
}

static void set_error(const char *msg)
{
    gtk_label_set_text(E.err, msg ? msg : "");
    gtk_widget_set_visible(GTK_WIDGET(E.err), msg != NULL);
}

/* ───────────── 表单构建 ───────────── */

static GtkWidget *expander_section(GtkWidget *parent, const char *title, gboolean expanded)
{
    GtkWidget *ex = gtk_expander_new(title);
    gtk_widget_add_css_class(ex, "section-expander");
    gtk_expander_set_expanded(GTK_EXPANDER(ex), expanded);
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "card");
    gtk_expander_set_child(GTK_EXPANDER(ex), card);
    gtk_box_append(GTK_BOX(parent), ex);
    return card;
}

static void iface_choices_reset(void)
{
    if (E.iface_choices) {
        for (guint i = 0; i < E.iface_choices->len; i++) {
            NpChoice *c = &g_array_index(E.iface_choices, NpChoice, i);
            g_free((char *)c->value);
            g_free((char *)c->label);
        }
        g_array_unref(E.iface_choices);
    }
    E.iface_choices = g_array_new(FALSE, TRUE, sizeof(NpChoice));
}

static void iface_add(const char *value, const char *label)
{
    NpChoice c = { g_strdup(value), g_strdup(label) };
    g_array_append_val(E.iface_choices, c);
}

static void build_general(GtkWidget *form)
{
    NpConnModel *m = E.model;
    GtkWidget *card = np_section(form, "常规");
    E.g.id = np_form_entry(card, "名称", m->id, "连接名称");

    gboolean wifi = is_type(NM_SETTING_WIRELESS_SETTING_NAME), wired = is_type(NM_SETTING_WIRED_SETTING_NAME);
    if (wifi || wired) {
        iface_choices_reset();
        iface_add(NULL, "任意设备");
        const GPtrArray *devs = np->client ? nm_client_get_devices(np->client) : NULL;
        for (guint i = 0; devs && i < devs->len; i++) {
            NMDevice *d = devs->pdata[i];
            if ((wifi && NM_IS_DEVICE_WIFI(d)) || (wired && NM_IS_DEVICE_ETHERNET(d)))
                iface_add(nm_device_get_iface(d), nm_device_get_iface(d));
        }
        E.g.iface = np_form_choice(card, "绑定设备", (NpChoice *)E.iface_choices->data, E.iface_choices->len,
                                   m->interface_name, NULL);
    } else if (!is_type(NM_SETTING_VPN_SETTING_NAME)) {
        E.g.iface_entry = np_form_entry(card, is_type(NM_SETTING_WIREGUARD_SETTING_NAME) ? "接口名" : "绑定设备",
                                        m->interface_name, is_type(NM_SETTING_WIREGUARD_SETTING_NAME) ? "wg0" : "任意设备");
    }

    E.g.autoconnect = np_form_switch(card, "自动连接", "在可用时自动连接到此网络", m->autoconnect);
    E.g.priority = np_form_spin(card, "自动连接优先级（越大越优先）", -999, 999, m->autoconnect_priority);
    E.g.metered = np_form_choice(card, "按流量计费", np_metered_choices, NP_METERED_N,
                                 np_metered_choices[MIN((guint)m->metered, NP_METERED_N - 1)].value, NULL);
    E.g.all_users = np_form_switch(card, "所有用户均可使用", "关闭后仅当前用户可用", m->all_users);
}

static void update_security_visibility(void)
{
    if (!E.s.type) return;
    guint t = np_choice_selected(E.s.type);
    gtk_widget_set_visible(E.s.psk_box, t == NP_SEC_WPA_PSK || t == NP_SEC_SAE);
    gtk_widget_set_visible(E.s.wep_box, t == NP_SEC_WEP);
    gtk_widget_set_visible(E.s.eap_box, t == NP_SEC_EAP);
    guint eap = np_choice_selected(E.s.eap);
    gboolean tls = eap == NP_EAP_TLS;
    np_field_set_visible(E.s.phase2, eap == NP_EAP_PEAP || eap == NP_EAP_TTLS);
    np_field_set_visible(E.s.anon, eap == NP_EAP_PEAP || eap == NP_EAP_TTLS);
    np_field_set_visible(E.s.password, !tls);
    np_field_set_visible(E.s.cert, tls);
    np_field_set_visible(E.s.key, tls);
    np_field_set_visible(E.s.key_pw, tls);
    np_field_set_visible(E.s.ca, eap != NP_EAP_PWD);
    np_field_set_visible(E.s.system_ca, eap != NP_EAP_PWD);
}

static void build_wifi(GtkWidget *form)
{
    NpConnModel *m = E.model;
    GtkWidget *card = np_section(form, "Wi-Fi");
    E.w.ssid = np_form_entry(card, "网络名称 (SSID)", m->ssid, "SSID");
    E.w.mode = np_form_choice(card, "模式", mode_choices, 2, m->ap_mode ? "ap" : "infrastructure", NULL);
    E.w.band = np_form_choice(card, "频段", np_band_choices, NP_BAND_N, m->band, NULL);
    E.w.hidden = np_form_switch(card, "隐藏网络", "网络不广播 SSID 时打开", m->hidden);

    GtkWidget *sec = np_section(form, "安全");
    E.s.type = np_form_choice(sec, "安全类型", np_sec_choices, NP_SEC_N, np_sec_choices[m->security].value, NULL);
    g_signal_connect_swapped(E.s.type, "notify::selected", G_CALLBACK(update_security_visibility), NULL);

    E.s.psk_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    E.s.psk = np_form_secret(E.s.psk_box, "密码", m->psk, E.rc ? "留空则不修改" : "8–63 个字符");
    gtk_box_append(GTK_BOX(sec), E.s.psk_box);

    E.s.wep_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    E.s.wep = np_form_secret(E.s.wep_box, "WEP 密钥", m->wep_key, "5/13 位字符或 10/26 位十六进制，或密码短语");
    gtk_box_append(GTK_BOX(sec), E.s.wep_box);

    E.s.eap_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *eb = E.s.eap_box;
    E.s.eap = np_form_choice(eb, "认证方式", np_eap_choices, NP_EAP_N, np_eap_choices[m->eap].value, NULL);
    g_signal_connect_swapped(E.s.eap, "notify::selected", G_CALLBACK(update_security_visibility), NULL);
    E.s.phase2 = np_form_choice(eb, "内部认证", np_phase2_choices, NP_PHASE2_N, m->phase2, NULL);
    E.s.identity = np_form_entry(eb, "身份（用户名）", m->identity, NULL);
    E.s.anon = np_form_entry(eb, "匿名身份", m->anonymous_identity, "可选");
    E.s.password = np_form_secret(eb, "密码", m->eap_password, E.rc ? "留空则不修改" : NULL);
    E.s.cert = np_form_file(eb, "客户端证书", m->client_cert, "PEM / DER / PKCS#12");
    E.s.key = np_form_file(eb, "私钥", m->private_key, NULL);
    E.s.key_pw = np_form_secret(eb, "私钥密码", m->private_key_password, "可选");
    E.s.ca = np_form_file(eb, "CA 证书", m->ca_cert, "留空则不验证服务器（不安全）");
    E.s.system_ca = np_form_switch(eb, "使用系统 CA 证书", NULL, m->system_ca_certs);
    E.s.domain = np_form_entry(eb, "服务器域名匹配", m->domain_suffix_match, "例如 radius.example.com");
    gtk_box_append(GTK_BOX(sec), eb);
    update_security_visibility();

    GtkWidget *adv = expander_section(form, "高级 Wi-Fi 选项", FALSE);
    E.w.mac = np_form_choice(adv, "MAC 地址", np_mac_choices, NP_MAC_N, m->cloned_mac, m->cloned_mac);
    E.w.mtu = np_form_spin(adv, "MTU（0 为自动）", 0, 9000, m->mtu);
}

static void build_wired(GtkWidget *form)
{
    NpConnModel *m = E.model;
    GtkWidget *card = np_section(form, "有线");
    E.e.mac = np_form_choice(card, "MAC 地址", np_mac_choices, NP_MAC_N, m->cloned_mac, m->cloned_mac);
    E.e.mtu = np_form_spin(card, "MTU（0 为自动）", 0, 9000, m->mtu);
}

static void update_wg_pub(void)
{
    char *priv = np_entry_text(E.wg.priv);
    char *pub = *priv ? np_wg_public_key(priv) : NULL;
    gtk_label_set_text(E.wg.pub, pub ? pub : (*priv ? "私钥无效" : "—"));
    g_free(pub);
    g_free(priv);
}

static void on_wg_generate(void)
{
    char *k = np_wg_generate_private_key();
    gtk_editable_set_text(GTK_EDITABLE(E.wg.priv), k);
    gtk_entry_set_visibility(E.wg.priv, TRUE);
    g_free(k);
}

static void on_copy_pub(void)
{
    const char *pub = gtk_label_get_text(E.wg.pub);
    if (np_wg_key_valid(pub)) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(E.wg.pub)), pub);
        np_notify(NULL, "公钥已复制");
    }
}

static void remove_peer(PeerForm *pf)
{
    gtk_box_remove(GTK_BOX(E.wg.peers_box), pf->card);
    g_ptr_array_remove(E.wg.peers, pf);
}

static void add_peer_form(const NpWgPeerModel *p)
{
    PeerForm *pf = g_new0(PeerForm, 1);
    pf->card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(pf->card, "card");
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(h), np_label("Peer", "key", 0));
    gtk_box_append(GTK_BOX(h), np_spacer());
    gtk_box_append(GTK_BOX(h), np_button("移除", "destructive", G_CALLBACK(remove_peer), pf));
    gtk_box_append(GTK_BOX(pf->card), h);
    pf->pub = np_form_entry(pf->card, "公钥", p ? p->public_key : NULL, "Base64");
    pf->endpoint = np_form_entry(pf->card, "Endpoint", p ? p->endpoint : NULL, "host:51820");
    pf->allowed = np_form_entry(pf->card, "AllowedIPs", p ? p->allowed_ips : "0.0.0.0/0, ::/0", NULL);
    pf->psk = np_form_secret(pf->card, "预共享密钥", p ? p->preshared_key : NULL, "可选");
    pf->keepalive = np_form_spin(pf->card, "保活间隔（秒，0 为关闭）", 0, 65535, p ? p->keepalive : 0);
    gtk_box_append(GTK_BOX(E.wg.peers_box), pf->card);
    g_ptr_array_add(E.wg.peers, pf);
}

static void on_add_peer(void)
{
    add_peer_form(NULL);
}

static void build_wireguard(GtkWidget *form)
{
    NpConnModel *m = E.model;
    GtkWidget *card = np_section(form, "WireGuard");
    E.wg.priv = np_form_secret(card, "私钥", m->wg_private_key, "Base64");
    g_signal_connect_swapped(E.wg.priv, "changed", G_CALLBACK(update_wg_pub), NULL);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(row), np_button("生成新密钥", NULL, G_CALLBACK(on_wg_generate), NULL));
    gtk_box_append(GTK_BOX(row), np_button("复制公钥", NULL, G_CALLBACK(on_copy_pub), NULL));
    gtk_box_append(GTK_BOX(card), row);
    GtkWidget *pf = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(pf), np_label("公钥", "key", 0));
    E.wg.pub = GTK_LABEL(np_label("—", "val", 0));
    gtk_label_set_selectable(E.wg.pub, TRUE);
    gtk_label_set_wrap(E.wg.pub, TRUE);
    gtk_label_set_wrap_mode(E.wg.pub, PANGO_WRAP_CHAR);
    gtk_box_append(GTK_BOX(pf), GTK_WIDGET(E.wg.pub));
    gtk_box_append(GTK_BOX(card), pf);
    char *port = m->wg_listen_port ? g_strdup_printf("%u", m->wg_listen_port) : NULL;
    E.wg.port = np_form_entry(card, "监听端口", port, "随机");
    g_free(port);
    E.wg.mtu = np_form_spin(card, "MTU（0 为自动）", 0, 9000, m->mtu);
    update_wg_pub();

    gtk_box_append(GTK_BOX(form), np_label("Peers", "section", 0));
    E.wg.peers_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(form), E.wg.peers_box);
    if (E.wg.peers) g_ptr_array_unref(E.wg.peers);
    E.wg.peers = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < m->wg_peers->len; i++) add_peer_form(m->wg_peers->pdata[i]);
    GtkWidget *add = np_glyph_button(NP_G_PLUS, "添加 Peer", "flat", G_CALLBACK(on_add_peer), NULL);
    gtk_widget_set_halign(add, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(form), add);
}

static void build_vpn(GtkWidget *form)
{
    NpConnModel *m = E.model;
    GtkWidget *card = np_section(form, "VPN");
    E.v.service = np_form_entry(card, "服务类型", m->vpn_service, "org.freedesktop.NetworkManager.openvpn");
    E.v.user = np_form_entry(card, "用户名", m->vpn_user, "可选");
    E.v.data = np_form_text(card, "插件参数", m->vpn_data, "每行 key = value，参数名见对应 VPN 插件的文档");
    E.v.secrets = np_form_text(card, "密钥", NULL, "已保存的密钥不会显示；需要修改时按 key = value 填写，留空保持不变");
}

static void update_ip_visibility(IpForm *f)
{
    const char *m = gtk_string_object_get_string(GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(f->method)));
    gboolean off = !g_strcmp0(m, "禁用") || !g_strcmp0(m, "忽略");
    gtk_widget_set_visible(f->fields, !off);
}

static void build_ip(GtkWidget *form, IpForm *f, const NpIpModel *ip, gboolean v6, gboolean expanded)
{
    GtkWidget *card = expander_section(form, v6 ? "IPv6" : "IPv4", expanded);
    f->method = v6 ? np_form_choice(card, "方式", np_ip6_method_choices, NP_IP6_METHOD_N, ip->method, NULL)
                   : np_form_choice(card, "方式", np_ip4_method_choices, NP_IP4_METHOD_N, ip->method, NULL);
    g_signal_connect_swapped(f->method, "notify::selected", G_CALLBACK(update_ip_visibility), f);
    f->fields = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    f->addresses = np_form_text(f->fields, "地址", ip->addresses, v6 ? "每行一个，如 fd00::10/64" : "每行一个，如 192.168.1.10/24（手动模式必填）");
    f->gateway = np_form_entry(f->fields, "网关", ip->gateway, NULL);
    f->dns = np_form_entry(f->fields, "DNS 服务器", ip->dns, "逗号分隔；留空使用自动获取的");
    f->ignore_auto_dns = np_form_switch(f->fields, "仅使用上面的 DNS", "忽略 DHCP / RA 下发的 DNS", ip->ignore_auto_dns);
    f->search = np_form_entry(f->fields, "搜索域", ip->dns_search, "逗号分隔");
    f->routes = np_form_text(f->fields, "静态路由", ip->routes, "每行：目标/前缀 [via 网关] [metric 跃点]");
    f->never_default = np_form_switch(f->fields, "仅用于此网络的资源", "不把默认路由指向此连接", ip->never_default);
    if (v6) {
        char pv[4];
        g_snprintf(pv, sizeof pv, "%d", ip->ip6_privacy);
        f->privacy = np_form_choice(f->fields, "隐私扩展", np_privacy_choices, NP_PRIVACY_N, pv, NULL);
    }
    gtk_box_append(GTK_BOX(card), f->fields);
    update_ip_visibility(f);
}

static void build_proxy(GtkWidget *form)
{
    GtkWidget *card = expander_section(form, "代理", E.model->proxy_auto);
    E.px.on = np_form_switch(card, "自动代理 (PAC)", NULL, E.model->proxy_auto);
    E.px.pac = np_form_entry(card, "PAC 地址", E.model->pac_url, "http://wpad/wpad.dat，留空则使用 WPAD");
}

static void build_form(void)
{
    memset(&E.g, 0, sizeof E.g);
    memset(&E.w, 0, sizeof E.w);
    memset(&E.s, 0, sizeof E.s);
    memset(&E.e, 0, sizeof E.e);
    memset(&E.v, 0, sizeof E.v);
    memset(&E.px, 0, sizeof E.px);
    memset(&E.ip4, 0, sizeof E.ip4);
    memset(&E.ip6, 0, sizeof E.ip6);
    E.wg.priv = NULL;

    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    build_general(form);
    if (is_type(NM_SETTING_WIRELESS_SETTING_NAME)) build_wifi(form);
    else if (is_type(NM_SETTING_WIRED_SETTING_NAME)) build_wired(form);
    else if (is_type(NM_SETTING_WIREGUARD_SETTING_NAME)) build_wireguard(form);
    else if (is_type(NM_SETTING_VPN_SETTING_NAME)) build_vpn(form);

    gboolean manual4 = !g_strcmp0(E.model->ip4.method, "manual") || !g_strcmp0(E.model->ip4.method, "shared");
    build_ip(form, &E.ip4, &E.model->ip4, FALSE, manual4 || is_type(NM_SETTING_WIREGUARD_SETTING_NAME));
    build_ip(form, &E.ip6, &E.model->ip6, TRUE, !g_strcmp0(E.model->ip6.method, "manual"));
    build_proxy(form);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(E.holder), form);
    gtk_widget_set_margin_end(form, 8);
    gtk_widget_set_sensitive(E.save, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(E.g.id));
}

/* ───────────── 表单 → 模型 ───────────── */

static void take(char **field, char *value)
{
    g_free(*field);
    *field = value;
}

static void collect_ip(NpIpModel *ip, IpForm *f, gboolean v6)
{
    np_set_str(&ip->method, np_choice_value(f->method, v6 ? np_ip6_method_choices : np_ip4_method_choices,
                                            v6 ? NP_IP6_METHOD_N : NP_IP4_METHOD_N));
    take(&ip->addresses, np_text_view_text(f->addresses));
    take(&ip->gateway, np_entry_text(f->gateway));
    take(&ip->dns, np_entry_text(f->dns));
    take(&ip->dns_search, np_entry_text(f->search));
    take(&ip->routes, np_text_view_text(f->routes));
    ip->never_default = gtk_switch_get_active(f->never_default);
    ip->ignore_auto_dns = gtk_switch_get_active(f->ignore_auto_dns);
    if (v6) ip->ip6_privacy = (int)np_choice_selected(f->privacy) - 1;
}

static NpConnModel *collect(GError **error)
{
    NpConnModel *m = np_conn_model_new(E.model->type);
    np_set_str(&m->uuid, E.model->uuid);
    g_ptr_array_set_size(m->wg_peers, 0);

    take(&m->id, np_entry_text(E.g.id));
    if (E.g.iface) np_set_str(&m->interface_name, np_choice_value(E.g.iface, (NpChoice *)E.iface_choices->data, E.iface_choices->len));
    else if (E.g.iface_entry) take(&m->interface_name, np_entry_text(E.g.iface_entry));
    else np_set_str(&m->interface_name, NULL);
    m->autoconnect = gtk_switch_get_active(E.g.autoconnect);
    m->autoconnect_priority = gtk_spin_button_get_value_as_int(E.g.priority);
    m->metered = (NMMetered)np_choice_selected(E.g.metered);
    m->all_users = gtk_switch_get_active(E.g.all_users);

    if (E.w.ssid) {
        np_set_str(&m->ssid, gtk_editable_get_text(GTK_EDITABLE(E.w.ssid))); /* SSID 不去空白 */
        m->ap_mode = np_choice_selected(E.w.mode) == 1;
        np_set_str(&m->band, np_choice_value(E.w.band, np_band_choices, NP_BAND_N));
        m->hidden = gtk_switch_get_active(E.w.hidden);
        np_set_str(&m->cloned_mac, np_choice_value(E.w.mac, np_mac_choices, NP_MAC_N));
        m->mtu = gtk_spin_button_get_value_as_int(E.w.mtu);

        m->security = (NpSecType)np_choice_selected(E.s.type);
        np_set_str(&m->psk, gtk_editable_get_text(GTK_EDITABLE(E.s.psk)));
        np_set_str(&m->wep_key, gtk_editable_get_text(GTK_EDITABLE(E.s.wep)));
        m->eap = (NpEapMethod)np_choice_selected(E.s.eap);
        np_set_str(&m->phase2, np_choice_value(E.s.phase2, np_phase2_choices, NP_PHASE2_N));
        take(&m->identity, np_entry_text(E.s.identity));
        take(&m->anonymous_identity, np_entry_text(E.s.anon));
        np_set_str(&m->eap_password, gtk_editable_get_text(GTK_EDITABLE(E.s.password)));
        take(&m->ca_cert, np_entry_text(E.s.ca));
        m->system_ca_certs = gtk_switch_get_active(E.s.system_ca);
        take(&m->domain_suffix_match, np_entry_text(E.s.domain));
        take(&m->client_cert, np_entry_text(E.s.cert));
        take(&m->private_key, np_entry_text(E.s.key));
        np_set_str(&m->private_key_password, gtk_editable_get_text(GTK_EDITABLE(E.s.key_pw)));
    }
    if (E.e.mac) {
        np_set_str(&m->cloned_mac, np_choice_value(E.e.mac, np_mac_choices, NP_MAC_N));
        m->mtu = gtk_spin_button_get_value_as_int(E.e.mtu);
    }
    if (E.wg.priv) {
        take(&m->wg_private_key, np_entry_text(E.wg.priv));
        char *port = np_entry_text(E.wg.port);
        guint64 v = 0;
        if (*port && (!g_ascii_string_to_unsigned(port, 10, 0, 65535, &v, NULL))) {
            g_free(port);
            g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "监听端口应为 0–65535");
            np_conn_model_free(m);
            return NULL;
        }
        g_free(port);
        m->wg_listen_port = (guint)v;
        m->mtu = gtk_spin_button_get_value_as_int(E.wg.mtu);
        for (guint i = 0; i < E.wg.peers->len; i++) {
            PeerForm *pf = E.wg.peers->pdata[i];
            NpWgPeerModel *p = np_wg_peer_model_new();
            take(&p->public_key, np_entry_text(pf->pub));
            take(&p->endpoint, np_entry_text(pf->endpoint));
            take(&p->allowed_ips, np_entry_text(pf->allowed));
            take(&p->preshared_key, np_entry_text(pf->psk));
            p->keepalive = gtk_spin_button_get_value_as_int(pf->keepalive);
            g_ptr_array_add(m->wg_peers, p);
        }
    }
    if (E.v.service) {
        take(&m->vpn_service, np_entry_text(E.v.service));
        take(&m->vpn_user, np_entry_text(E.v.user));
        take(&m->vpn_data, np_text_view_text(E.v.data));
        take(&m->vpn_secrets, np_text_view_text(E.v.secrets));
    }
    collect_ip(&m->ip4, &E.ip4, FALSE);
    collect_ip(&m->ip6, &E.ip6, TRUE);
    m->proxy_auto = gtk_switch_get_active(E.px.on);
    take(&m->pac_url, np_entry_text(E.px.pac));
    return m;
}

/* ───────────── 保存 ───────────── */

typedef struct {
    guint gen;
    NMRemoteConnection *rc;
    NMConnection *nc;
    gboolean apply_live;
    gboolean activate;
} Save;

static void save_free(Save *s)
{
    g_clear_object(&s->rc);
    g_clear_object(&s->nc);
    g_free(s);
}

static void close_editor(void)
{
    gboolean from_new = E.from_new_page;
    np_nav_back();
    if (from_new && !g_strcmp0(np_nav_current(), NP_PAGE_NEW)) np_nav_back();
}

static void update_done(GObject *src, GAsyncResult *res, gpointer data)
{
    Save *s = data;
    GError *e = NULL;
    GVariant *r = nm_remote_connection_update2_finish(NM_REMOTE_CONNECTION(src), res, &e);
    if (r) g_variant_unref(r);
    gboolean current = s->gen == E.gen;
    if (current) gtk_widget_set_sensitive(E.save, TRUE);
    if (e) {
        if (current) set_error(e->message);
        else np_notify("保存失败", "%s", e->message);
        g_error_free(e);
    } else {
        np_notify(NULL, "已保存 %s", nm_connection_get_id(s->nc));
        if (s->apply_live) np_apply_to_active(s->rc, s->nc, FALSE);
        if (current) close_editor();
    }
    save_free(s);
}

static void add_done(GObject *src, GAsyncResult *res, gpointer data)
{
    Save *s = data;
    GError *e = NULL;
    NMRemoteConnection *rc = nm_client_add_connection2_finish(NM_CLIENT(src), res, NULL, &e);
    gboolean current = s->gen == E.gen;
    if (current) gtk_widget_set_sensitive(E.save, TRUE);
    if (e) {
        if (current) set_error(e->message);
        else np_notify("保存失败", "%s", e->message);
        g_error_free(e);
    } else {
        if (s->activate) np_activate(rc, NULL, NULL);
        else np_notify(NULL, "已添加 %s", nm_connection_get_id(NM_CONNECTION(rc)));
        g_object_unref(rc);
        if (current) close_editor();
    }
    save_free(s);
}

/* 只改了 “connection” 设置（名称、自动连接等）时不需要热应用 */
static gboolean needs_live_apply(NMConnection *old, NMConnection *new)
{
    GHashTable *diff = NULL;
    if (nm_connection_diff(old, new, NM_SETTING_COMPARE_FLAG_EXACT, &diff)) return FALSE;
    gboolean need = FALSE;
    GHashTableIter it;
    gpointer k;
    g_hash_table_iter_init(&it, diff);
    while (g_hash_table_iter_next(&it, &k, NULL))
        if (g_strcmp0(k, NM_SETTING_CONNECTION_SETTING_NAME)) need = TRUE;
    g_hash_table_unref(diff);
    return need;
}

static void on_save(void)
{
    if (!E.model) return;
    GError *e = NULL;
    NpConnModel *m = collect(&e);
    NMConnection *nc = NULL;
    if (m) {
        if (E.base) {
            nc = nm_simple_connection_new_clone(E.base);
            if (!np_conn_model_apply(m, nc, &e)) g_clear_object(&nc);
        } else {
            nc = np_conn_model_create_connection(m, &e);
        }
    }
    np_conn_model_free(m);
    if (!nc) {
        set_error(e->message);
        g_error_free(e);
        return;
    }
    set_error(NULL);
    gtk_widget_set_sensitive(E.save, FALSE);

    Save *s = g_new0(Save, 1);
    s->gen = E.gen;
    s->nc = nc;
    s->activate = E.activate_after;
    GVariant *dict = nm_connection_to_dbus(nc, NM_CONNECTION_SERIALIZE_ALL);
    if (E.rc) {
        s->rc = g_object_ref(E.rc);
        s->apply_live = np_active_for(E.rc) && needs_live_apply(E.base, nc);
        nm_remote_connection_update2(E.rc, dict, NM_SETTINGS_UPDATE2_FLAG_TO_DISK, NULL, NULL, update_done, s);
    } else {
        nm_client_add_connection2(np->client, dict, NM_SETTINGS_ADD_CONNECTION2_FLAG_TO_DISK, NULL, FALSE, NULL,
                                  add_done, s);
    }
}

/* ───────────── 打开 / 关闭 ───────────── */

static void reset(void)
{
    E.gen++;
    g_clear_object(&E.rc);
    g_clear_object(&E.base);
    g_clear_pointer(&E.model, np_conn_model_free);
    E.pending = 0;
    E.activate_after = FALSE;
    set_error(NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(E.holder), NULL);
    if (E.wg.peers) g_ptr_array_set_size(E.wg.peers, 0);
}

void np_page_editor_leave(void)
{
    reset();
}

typedef struct {
    guint gen;
    char *setting;
} SecretsReq;

static void secrets_done(GObject *src, GAsyncResult *res, gpointer data)
{
    SecretsReq *q = data;
    GError *e = NULL;
    GVariant *v = nm_remote_connection_get_secrets_finish(NM_REMOTE_CONNECTION(src), res, &e);
    if (q->gen == E.gen) {
        if (v) nm_connection_update_secrets(E.base, q->setting, v, NULL);
        else g_debug("get secrets %s: %s", q->setting, e->message);
        if (--E.pending == 0) {
            E.model = np_conn_model_from_connection(E.base);
            build_form();
        }
    }
    if (v) g_variant_unref(v);
    if (e) g_error_free(e);
    g_free(q->setting);
    g_free(q);
}

static void open_common(const char *title)
{
    gtk_label_set_text(E.title, title);
    E.from_new_page = !g_strcmp0(np_nav_current(), NP_PAGE_NEW);
    np_nav_push(NP_PAGE_EDITOR);
}

void np_page_editor_open(NMRemoteConnection *rc)
{
    reset();
    E.rc = g_object_ref(rc);
    E.base = nm_simple_connection_new_clone(NM_CONNECTION(rc));
    gtk_widget_set_sensitive(E.save, FALSE);
    char *t = g_strdup_printf("编辑 %s", nm_connection_get_id(NM_CONNECTION(rc)));
    open_common(t);
    g_free(t);

    static const char *const secret_settings[] = {
        NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NM_SETTING_802_1X_SETTING_NAME, NM_SETTING_WIREGUARD_SETTING_NAME,
    };
    for (guint i = 0; i < G_N_ELEMENTS(secret_settings); i++) {
        if (!nm_connection_get_setting_by_name(E.base, secret_settings[i])) continue;
        SecretsReq *q = g_new0(SecretsReq, 1);
        q->gen = E.gen;
        q->setting = g_strdup(secret_settings[i]);
        E.pending++;
        nm_remote_connection_get_secrets_async(rc, q->setting, NULL, secrets_done, q);
    }
    if (E.pending == 0) {
        E.model = np_conn_model_from_connection(E.base);
        build_form();
    }
}

void np_page_editor_open_new(NpConnModel *model, NMConnection *base, gboolean activate_after)
{
    reset();
    E.model = model;
    E.base = base;
    E.activate_after = activate_after;
    char *t = g_strdup_printf("新建%s", model->ap_mode ? "热点" : "连接");
    open_common(t);
    g_free(t);
    build_form();
}

GtkWidget *np_page_editor_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(page), np_header("", &E.title, TRUE));
    E.holder = np_scroller(NULL, 600);
    gtk_box_append(GTK_BOX(page), E.holder);

    E.err = GTK_LABEL(np_label("", "error", 0));
    gtk_label_set_wrap(E.err, TRUE);
    gtk_widget_set_margin_end(GTK_WIDGET(E.err), 8);
    gtk_widget_set_visible(GTK_WIDGET(E.err), FALSE);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(E.err));

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(btns, "footer");
    gtk_box_append(GTK_BOX(btns), np_spacer());
    gtk_box_append(GTK_BOX(btns), np_button("取消", NULL, G_CALLBACK(np_nav_back), NULL));
    E.save = np_button("保存", "suggested", G_CALLBACK(on_save), NULL);
    gtk_box_append(GTK_BOX(btns), E.save);
    gtk_box_append(GTK_BOX(page), btns);
    return page;
}
