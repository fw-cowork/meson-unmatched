project('hifive-unmatched-lite', meson_version: '>=0.60.0')

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

run_target('info', command: [py, driver, 'info'] + driver_args)
run_target('check', command: [py, driver, 'check'] + driver_args)
run_target('fetch-sources', command: [py, driver, 'fetch'] + driver_args)
run_target('opensbi-fw', command: [py, driver, 'opensbi'] + driver_args)
run_target('u-boot', command: [py, driver, 'u-boot'] + driver_args)
run_target('qemu-u-boot', command: [py, driver, 'qemu-u-boot'] + driver_args)
run_target('linux', command: [py, driver, 'linux'] + driver_args)
run_target('dev-linux', command: [py, driver, 'dev-linux'] + driver_args)
run_target('dev-uboot', command: [py, driver, 'dev-uboot'] + driver_args)
run_target('fit', command: [py, driver, 'fit'] + driver_args)
run_target('firmware-fit', command: [py, driver, 'firmware-fit'] + driver_args)
run_target('busybox', command: [py, driver, 'busybox'] + driver_args)
run_target('rootfs', command: [py, driver, 'rootfs'] + driver_args)
run_target('bootchain', command: [py, driver, 'bootchain'] + driver_args)
run_target('sd-image', command: [py, driver, 'sd-image'] + driver_args)
run_target('qemu-image', command: [py, driver, 'qemu-image', '--profile', 'qemu'] + driver_args)
run_target('clean-lite', command: [py, driver, 'clean'] + driver_args)
