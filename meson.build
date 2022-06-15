project(
  'sample-native-wayland-compositor', 'c',
  version: '0.0.1',
  license: 'Apache-2.0',
  meson_version: '>= 0.52.1',
  default_options: [ 'warning_level=3', 'werror=true', 'optimization=2', 'c_std=gnu11' ],
)

cc = meson.get_compiler('c')

# generic version requirements

udev_req = '>= 136'
systemd_req = '>= 209'

# dependencies

udev_dep = dependency('libudev', version: udev_req)
systemd_dep = dependency('libsystemd', version: systemd_req)
pam_dep = cc.find_library('pam')

# config.h

cdata = configuration_data()
cdata.set_quoted('VERSION', meson.project_version())

subdir('playground')
subdir('src')
subdir('launcher')
