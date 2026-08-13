baremetal_string_lib = static_library(
  'baremetal-string',
  files('string.c'),
  dependencies: baremetal_target_dep,
  pic: false,
  build_by_default: false,
)
baremetal_string_dep = declare_dependency(
  link_with: baremetal_string_lib,
  dependencies: baremetal_target_dep,
)
