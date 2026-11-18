#include "np-config.h"
#include "np-util.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_defaults(void)
{
    g_autoptr(NpConfig) c = np_config_new();
    g_assert_null(c->colors_css);
    g_assert_true(g_str_has_suffix(c->style_css, "netpop/style.css"));
    g_assert_true(c->anchor_top && c->anchor_right);
    g_assert_false(c->anchor_bottom || c->anchor_left);
    g_assert_cmpint(c->margin_top, ==, 6);
    g_assert_cmpint(c->width, ==, 368);
    g_assert_true(c->agent && c->keyring && c->hide_on_focus_loss);
    char *p = np_config_default_path();
    g_assert_true(g_str_has_suffix(p, "netpop/netpop.conf"));
    g_free(p);
}

static void test_load(void)
{
    const char *data =
        "[appearance]\ncolors = ~/theme.css\nwidth = 400\n"
        "[popup]\nanchor = bottom-left\nmargin-top = 0\nmargin-left = 12\nhide-on-focus-loss = false\n"
        "[agent]\nenabled = false\nkeyring = false\n";
    g_autoptr(NpConfig) c = np_config_new();
    GError *e = NULL;
    g_assert_true(np_config_load_data(c, data, -1, &e));
    g_assert_no_error(e);
    char *expect = g_build_filename(g_get_home_dir(), "theme.css", NULL);
    g_assert_cmpstr(c->colors_css, ==, expect);
    g_free(expect);
    g_assert_cmpint(c->width, ==, 400);
    g_assert_true(c->anchor_bottom && c->anchor_left);
    g_assert_false(c->anchor_top || c->anchor_right);
    g_assert_cmpint(c->margin_left, ==, 12);
    g_assert_false(c->hide_on_focus_loss);
    g_assert_false(c->agent);
    g_assert_false(c->keyring);
}

static void test_anchor(void)
{
    g_autoptr(NpConfig) c = np_config_new();
    g_assert_true(np_config_set_anchor(c, "center", NULL));
    g_assert_false(c->anchor_top || c->anchor_bottom || c->anchor_left || c->anchor_right);
    g_assert_true(np_config_set_anchor(c, "top", NULL));
    g_assert_true(c->anchor_top && !c->anchor_left && !c->anchor_right);
    GError *e = NULL;
    g_assert_false(np_config_set_anchor(c, "middle-ish", &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_INVALID);
    g_clear_error(&e);
}

static void test_errors(void)
{
    const char *bad[] = {
        "[popup]\nanchor = sideways\n",
        "[popup]\nmargin-top = abc\n",
        "[popup]\nmargin-top = -5\n",
        "[appearance]\nwidth = 10\n",
        "[agent]\nenabled = maybe\n",
        "not a key file",
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(NpConfig) c = np_config_new();
        GError *e = NULL;
        g_assert_false(np_config_load_data(c, bad[i], -1, &e));
        g_assert_nonnull(e);
        g_clear_error(&e);
    }
}

static void test_file(void)
{
    g_autoptr(NpConfig) c = np_config_new();
    GError *e = NULL;
    g_assert_true(np_config_load_file(c, "/nonexistent/netpop.conf", &e)); /* 不存在：用默认值 */
    g_assert_no_error(e);

    char *dir = g_dir_make_tmp("netpop-test-XXXXXX", NULL);
    char *path = g_build_filename(dir, "netpop.conf", NULL);
    g_assert_true(g_file_set_contents(path, "[popup]\nmargin-top = 42\n", -1, NULL));
    g_assert_true(np_config_load_file(c, path, &e));
    g_assert_cmpint(c->margin_top, ==, 42);

    g_assert_true(g_file_set_contents(path, "[popup]\nmargin-top = x\n", -1, NULL));
    g_assert_false(np_config_load_file(c, path, &e));
    g_assert_nonnull(strstr(e->message, path)); /* 错误信息带文件路径 */
    g_clear_error(&e);

    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/config/defaults", test_defaults);
    g_test_add_func("/config/load", test_load);
    g_test_add_func("/config/anchor", test_anchor);
    g_test_add_func("/config/errors", test_errors);
    g_test_add_func("/config/file", test_file);
    return g_test_run();
}
