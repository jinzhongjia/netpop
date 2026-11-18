core_sources = files(
  'core/np-ap.c',
  'core/np-config.c',
  'core/np-conn-model.c',
  'core/np-keyring.c',
  'core/np-secrets.c',
  'core/np-util.c',
  'core/np-wg.c',
)

core_deps = [glib, gio, libnm]
if secret.found()
  core_deps += secret
endif

# 核心库：不依赖 GTK，单元测试直接链接它
libnpcore = static_library('npcore', core_sources,
  dependencies: core_deps,
  include_directories: root_inc,
)

npcore_dep = declare_dependency(
  link_with: libnpcore,
  include_directories: [root_inc, include_directories('core')],
  dependencies: core_deps,
)

resources = gnome.compile_resources('np-resources',
  'ui/netpop.gresource.xml',
  source_dir: 'ui',
  c_name: 'np',
)

ui_sources = files(
  'main.c',
  'ui/np-actions.c',
  'ui/np-agent.c',
  'ui/np-app.c',
  'ui/np-page-auth.c',
  'ui/np-page-connections.c',
  'ui/np-page-details.c',
  'ui/np-page-editor.c',
  'ui/np-page-wifi.c',
  'ui/np-widgets.c',
)

# gtk4-layer-shell 需要在 libwayland-client 之前链接，因此排在最前面
executable('netpop', ui_sources, resources,
  dependencies: [layer, gtk, npcore_dep],
  include_directories: include_directories('ui'),
  install: true,
)
