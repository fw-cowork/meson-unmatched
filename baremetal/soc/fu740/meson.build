baremetal_fu740_lib = static_library(
  'baremetal-fu740',
  files('clock.c'),
  dependencies: baremetal_target_dep,
  pic: false,
  build_by_default: false,
)
baremetal_fu740_dep = declare_dependency(
  link_with: baremetal_fu740_lib,
  dependencies: baremetal_target_dep,
)
