baremetal_uboot_go_link_depends = files('u-boot-go.lds')
baremetal_uboot_go_linker_script_path = meson.current_source_dir() / 'u-boot-go.lds'
baremetal_link_args = [
  '-nostdlib',
  '-nostartfiles',
  '-nodefaultlibs',
  '-static',
  '-no-pie',
  '-Wl,-T,' + baremetal_uboot_go_linker_script_path,
  '-Wl,--gc-sections',
  '-Wl,--no-relax',
  '-Wl,--build-id=none',
]

baremetal_uboot_go_startup_lib = static_library(
  'baremetal-u-boot-go-startup',
  files('start.S'),
  dependencies: baremetal_target_dep,
  pic: false,
  build_by_default: false,
)

# Link the entry object unconditionally and export the freestanding link policy.
baremetal_uboot_go_dep = declare_dependency(
  link_whole: baremetal_uboot_go_startup_lib,
  link_args: baremetal_link_args,
  dependencies: baremetal_target_dep,
)
