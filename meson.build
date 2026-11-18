project('netpop', 'c',
  version: '0.2.0',
  meson_version: '>= 0.64',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

app_id = 'dev.netpop.Netpop'
gnome = import('gnome')
cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
    '-Wno-cast-function-type',
    '-Werror=implicit-function-declaration',
    '-Werror=incompatible-pointer-types',
    '-Werror=return-type',
    '-Werror=format-security',
  ]),
  '-DG_LOG_DOMAIN="netpop"',
  language: 'c',
)

glib   = dependency('glib-2.0', version: '>= 2.76')
gio    = dependency('gio-2.0',  version: '>= 2.76')
libnm  = dependency('libnm',    version: '>= 1.40')
gtk    = dependency('gtk4',     version: '>= 4.12')
layer  = dependency('gtk4-layer-shell-0', required: get_option('layer_shell'))
secret = dependency('libsecret-1', version: '>= 0.20', required: get_option('keyring'))

conf = configuration_data()
conf.set_quoted('NP_APP_ID', app_id)
conf.set_quoted('NP_VERSION', meson.project_version())
conf.set10('HAVE_LAYER_SHELL', layer.found())
conf.set10('HAVE_LIBSECRET', secret.found())
configure_file(output: 'config.h', configuration: conf)

root_inc = include_directories('.')

subdir('src')
subdir('data')
subdir('tests')

summary({
  'layer-shell': layer.found(),
  'keyring (libsecret)': secret.found(),
}, section: 'Features')
