baremetal_objcopy = find_program('objcopy')
baremetal_objdump = find_program('objdump')
baremetal_nm = find_program('nm')
baremetal_copy = find_program('cp')
baremetal_include = include_directories('include')
baremetal_linker_script = files('common/uboot-go.ld')
baremetal_linker_script_path = meson.current_source_dir() / 'common/uboot-go.ld'
baremetal_runtime = files(
  'common/start.S',
  'common/baremetal.c',
  'common/fu740.c',
  'common/sifive_pwm_math.c',
  'common/sifive_pwm.c',
)

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
]

baremetal_link_args = [
  '-nostdlib',
  '-nostartfiles',
  '-nodefaultlibs',
  '-static',
  '-no-pie',
  '-Wl,-T,' + baremetal_linker_script_path,
  '-Wl,--gc-sections',
  '-Wl,--no-relax',
  '-Wl,--build-id=none',
]

# Add each new U-Boot go program here. Values may contain multiple source files.
baremetal_programs = {
  'unmatched-led': files('programs/led/main.c'),
}

baremetal_artifacts = []
foreach program_name, program_sources : baremetal_programs
  program_map_private = meson.current_build_dir() / (program_name + '.elf.p') / (program_name + '.map')
  program_elf = executable(
    program_name,
    baremetal_runtime,
    program_sources,
    include_directories: baremetal_include,
    c_args: baremetal_c_args,
    link_args: baremetal_link_args + ['-Wl,-Map,' + program_map_private],
    link_depends: baremetal_linker_script,
    name_suffix: 'elf',
    build_by_default: false,
  )

  program_bin = custom_target(
    program_name + '-raw-image',
    input: program_elf,
    output: program_name + '.bin',
    command: [baremetal_objcopy, '-O', 'binary', '@INPUT@', '@OUTPUT@'],
    build_by_default: false,
  )

  program_map = custom_target(
    program_name + '-link-map',
    input: program_elf,
    output: program_name + '.map',
    command: [baremetal_copy, program_map_private, '@OUTPUT@'],
    build_by_default: false,
  )

  program_dis = custom_target(
    program_name + '-disassembly',
    input: program_elf,
    output: program_name + '.dis',
    command: [baremetal_objdump, '-d', '-S', '@INPUT@'],
    capture: true,
    build_by_default: false,
  )

  program_sym = custom_target(
    program_name + '-symbols',
    input: program_elf,
    output: program_name + '.sym',
    command: [baremetal_nm, '-n', '-S', '@INPUT@'],
    capture: true,
    build_by_default: false,
  )

  alias_target(program_name + '-bin', program_bin)
  program_artifacts = [program_bin, program_map, program_dis, program_sym]
  alias_target(program_name + '-artifacts', program_artifacts)
  baremetal_artifacts += program_artifacts
endforeach

alias_target('baremetal', baremetal_artifacts)
