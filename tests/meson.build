test_deps = [
  dependency('libsoup-3.0'),
  dependency('json-glib-1.0'),
  cc.find_library('m', required: false),
]

test_engine = executable('test-engine',
  [
    'engine-tests.c',
    '../src/velocity-speedtest.c',
    '../src/velocity-speedtest-ping.c',
    '../src/velocity-speedtest-transfer.c',
  ],
  include_directories: include_directories('../src'),
  dependencies: test_deps,
)

test('Test engine', test_engine,
  protocol: 'tap',
  timeout: 60,
)
