baremetal_include = include_directories('include')

baremetal_c_args = [
  '-O2',
  '-Wall',
  '-Wextra',
  '-Werror',
  '-ffreestanding',
  '-fno-builtin',
  '-fno-common',
  '-fno-pic',
  '-fno-pie',
  '-fno-stack-protector',
  '-fno-asynchronous-unwind-tables',
  '-fno-unwind-tables',
  '-ffunction-sections',
  '-fdata-sections',
  '-mno-relax',
  '-msmall-data-limit=0',
]

baremetal_headers_dep = declare_dependency(
  include_directories: baremetal_include,
)
baremetal_target_dep = declare_dependency(
  compile_args: baremetal_c_args,
  dependencies: baremetal_headers_dep,
)

# Source directories own their libraries and export dependency objects upward.
subdir('tools')
subdir('arch')
subdir('lib')
subdir('soc')
subdir('drivers')
subdir('tests')
subdir('apps')

baremetal_artifacts = []
foreach app : baremetal_apps
  app_name = app['name']
  app_build_subdir = app_name
  app_map_private = meson.current_build_dir() / app_build_subdir / (app_name + '.elf.p') / (app_name + '.map')
  app_elf = executable(
    app_name,
    app['sources'],
    dependencies: [baremetal_uboot_go_dep] + app['dependencies'],
    link_args: ['-Wl,-Map,' + app_map_private],
    link_depends: baremetal_uboot_go_link_depends,
    name_suffix: 'elf',
    build_subdir: app_build_subdir,
    build_by_default: false,
  )

  app_check = custom_target(
    app_name + '-image-check',
    input: app_elf,
    output: app_name + '.check',
    command: [
      baremetal_image_check,
      baremetal_readelf,
      baremetal_nm,
      '@INPUT@',
    ],
    capture: true,
    build_subdir: app_build_subdir,
    build_by_default: false,
  )

  app_bin = custom_target(
    app_name + '-raw-image',
    input: app_elf,
    output: app_name + '.bin',
    command: [baremetal_objcopy, '-O', 'binary', '@INPUT@', '@OUTPUT@'],
    build_subdir: app_build_subdir,
    build_by_default: false,
  )

  app_map = custom_target(
    app_name + '-link-map',
    input: app_elf,
    output: app_name + '.map',
    command: [baremetal_copy, app_map_private, '@OUTPUT@'],
    build_subdir: app_build_subdir,
    build_by_default: false,
  )

  app_dis = custom_target(
    app_name + '-disassembly',
    input: app_elf,
    output: app_name + '.dis',
    command: [baremetal_objdump, '-d', '-S', '@INPUT@'],
    capture: true,
    build_subdir: app_build_subdir,
    build_by_default: false,
  )

  app_sym = custom_target(
    app_name + '-symbols',
    input: app_elf,
    output: app_name + '.sym',
    command: [baremetal_nm, '-n', '-S', '@INPUT@'],
    capture: true,
    build_subdir: app_build_subdir,
    build_by_default: false,
  )

  alias_target(app_name + '-bin', [app_bin, app_check])
  app_artifacts = [app_bin, app_map, app_dis, app_sym, app_check]
  alias_target(app_name + '-artifacts', app_artifacts)
  baremetal_artifacts += app_artifacts
endforeach

alias_target('baremetal', baremetal_artifacts)
