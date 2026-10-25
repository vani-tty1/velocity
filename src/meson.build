blueprints = custom_target('blueprints',
  input: files(
    'velocity-window.blp',
    'shortcuts-dialog.blp',
  ),
  output: '.',
  command: [find_program('blueprint-compiler'), 'batch-compile', '@OUTPUT@', '@CURRENT_SOURCE_DIR@', '@INPUT@'],
)

velocity_sources = [
  'main.c',
  'velocity-application.c',
  'velocity-window.c',
  'velocity-speedtest.c',
  'velocity-speedtest-ping.c',
  'velocity-speedtest-servers.c',
  'velocity-speedtest-transfer.c',
]

velocity_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
  dependency('epoxy'),
  dependency('libsoup-3.0'),
  dependency('json-glib-1.0'),
  cc.find_library('m', required: false),
]

velocity_sources += gnome.compile_resources('velocity-resources',
  'velocity.gresource.xml',
  source_dir: [meson.current_build_dir(), meson.project_build_root() / 'data'],
  dependencies: [appstream_file, blueprints],
  c_name: 'velocity'
)

executable('velocity', velocity_sources,
  dependencies: velocity_deps,
       install: true,
)
