project('hifive-unmatched-lite', 'c', meson_version: '>=1.10.0')

py = find_program('python3')
driver = files('scripts/litebuild.py')

driver_args = []

cross_compile = meson.get_external_property('cross_compile', '')
if cross_compile != ''
  driver_args += ['--cross-compile', cross_compile]
endif

toolchain_bindir = meson.get_external_property('toolchain_bindir', '')
if toolchain_bindir != ''
  driver_args += ['--toolchain-bindir', toolchain_bindir]
endif

sys_root = meson.get_external_property('sys_root', '')
if sys_root != ''
  driver_args += ['--sysroot', sys_root]
endif

native_bindirs = meson.get_external_property('native_bindirs', [])
foreach bindir : native_bindirs
  driver_args += ['--native-bindir', bindir]
endforeach

native_sysroot_dirs = meson.get_external_property('native_sysroot_dirs', [])
foreach sysroot_dir : native_sysroot_dirs
  driver_args += ['--native-sysroot', sysroot_dir]
endforeach

# Keep the public Meson target names stable while describing the Python driver
# commands in one table.  Per-target arguments remain explicit for profiles
# whose defaults differ from the normal host workflow.
litebuild_targets = {
  'info': ['info'],
  'check': ['check'],
  'fetch-sources': ['fetch'],
  'opensbi-fw': ['opensbi'],
  'u-boot': ['u-boot'],
  'u-boot-mmode': ['u-boot-mmode'],
  'u-boot-lwip': ['u-boot-lwip'],
  'u-boot-lwip-port': ['u-boot-lwip-port'],
  'qemu-u-boot': ['qemu-u-boot'],
  'linux': ['linux'],
  'dev-linux': ['dev-linux'],
  'dev-uboot': ['dev-uboot'],
  'fit': ['fit'],
  'firmware-fit': ['firmware-fit'],
  'busybox': ['busybox'],
  'rootfs': ['rootfs'],
  'bootchain': ['bootchain'],
  'sd-image': ['sd-image'],
  'qemu-image': ['qemu-image', '--profile', 'qemu'],
  'clean-lite': ['clean'],
}

foreach target_name, target_args : litebuild_targets
  run_target(target_name, command: [py, driver] + target_args + driver_args)
endforeach

subdir('baremetal')
