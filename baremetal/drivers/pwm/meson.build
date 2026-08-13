baremetal_sifive_pwm_math_sources = files('sifive_pwm_math.c')
# Keep this source dependency machine-neutral: target firmware and native tests
# compile the same pure calculations with their respective compilers.
baremetal_sifive_pwm_math_dep = declare_dependency(
  sources: baremetal_sifive_pwm_math_sources,
  dependencies: baremetal_headers_dep,
)

baremetal_sifive_pwm_lib = static_library(
  'baremetal-sifive-pwm',
  baremetal_sifive_pwm_math_sources,
  files('sifive_pwm.c'),
  dependencies: baremetal_target_dep,
  pic: false,
  build_by_default: false,
)
baremetal_sifive_pwm_dep = declare_dependency(
  link_with: baremetal_sifive_pwm_lib,
  dependencies: baremetal_target_dep,
)
