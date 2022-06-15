config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata,
)

deps_zippo = [
  udev_dep,
]

srcs_zippo = [
  'main.c',
  'native.c',
  config_h,
]

executable(
  'zippo',
  srcs_zippo,
  install: false,
  dependencies: deps_zippo,
)
