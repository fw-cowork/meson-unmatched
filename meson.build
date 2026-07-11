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

native_bindirs = meson.get_external_property('native_bindirs', [])
foreach bindir : native_bindirs
  driver_args += ['--native-bindir', bindir]
endforeach

run_target('info', command: [py, driver, 'info'] + driver_args)
run_target('check', command: [py, driver, 'check'] + driver_args)
run_target('fetch-sources', command: [py, driver, 'fetch'] + driver_args)
run_target('opensbi-fw', command: [py, driver, 'opensbi'] + driver_args)
run_target('u-boot', command: [py, driver, 'u-boot'] + driver_args)
run_target('linux', command: [py, driver, 'linux'] + driver_args)
run_target('bootchain', command: [py, driver, 'bootchain'] + driver_args)
run_target('clean-lite', command: [py, driver, 'clean'] + driver_args)
