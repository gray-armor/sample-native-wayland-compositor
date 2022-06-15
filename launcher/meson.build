srcs_zippo_launch = [
  'launch.c',
  'main.c',
]

deps_zippo_launch = [
  pam_dep,
  systemd_dep,
]

executable(
  'zippo-launch',
  srcs_zippo_launch,
  install: false,
  dependencies: deps_zippo_launch,
)
