baremetal_host_tests = []

baremetal_pwm_math_test = executable(
  'baremetal-pwm-math-test',
  files('sifive_pwm_math_test.c'),
  dependencies: baremetal_sifive_pwm_math_dep,
  c_args: ['-Wall', '-Wextra', '-Werror'],
  native: true,
  build_by_default: false,
)
test('baremetal-pwm-math', baremetal_pwm_math_test, suite: 'baremetal')
baremetal_host_tests += [baremetal_pwm_math_test]

run_target(
  'baremetal-test',
  command: [baremetal_test_runner] + baremetal_host_tests,
)
