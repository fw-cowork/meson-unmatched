baremetal_sifive_uart_lib = static_library(
  'baremetal-sifive-uart',
  files('sifive_uart.c'),
  dependencies: baremetal_target_dep,
  pic: false,
  build_by_default: false,
)
baremetal_sifive_uart_dep = declare_dependency(
  link_with: baremetal_sifive_uart_lib,
  dependencies: baremetal_target_dep,
)
