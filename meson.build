project('zathura-djvu', 'c',
  version: '0.2.7',
  meson_version: '>=0.43',
  default_options: 'c_std=c11'
)

version = meson.project_version()
version_array = version.split('.')

cc = meson.get_compiler('c')

datadir = get_option('datadir')
desktopdir = join_paths(datadir, 'applications')

# required dependencies
zathura = dependency('zathura', version: '>=0.3.8')
djvu = dependency('ddjvuapi')

build_dependencies = [zathura, djvu]

# defines
defines = [
  '-DVERSION_MAJOR=@0@'.format(version_array[0]),
  '-DVERSION_MINOR=@0@'.format(version_array[1]),
  '-DVERSION_REV=@0@'.format(version_array[2]),
  '-D_DEFAULT_SOURCE',
]

# compile flags
flags = [
  '-Wall',
  '-Wextra',
  '-pedantic',
  '-Werror=implicit-function-declaration'
]
flags = cc.get_supported_arguments(flags)

sources = files(
  'djvu.c',
  'page-text.c'
)

djvu = shared_module('djvu',
  sources,
  dependencies: build_dependencies,
  c_args: defines + flags,
  install: true,
  install_dir: zathura.get_pkgconfig_variable('plugindir')
)

install_data('zathura-djvu.desktop', install_dir: desktopdir)
