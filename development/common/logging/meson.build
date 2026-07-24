logging_internal_include = include_directories('internal')

logging = static_library(
  'logging',
  sources: [
    'internal/ConsoleOutputs.cpp',
    'internal/Log.cpp',
    'internal/LogTargets.cpp',
    'internal/LogTools.cpp',
    'internal/Logger.cpp',
  ],
  include_directories: [common_include, logging_internal_include],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [fmt_dep, threads_dep],
  pic: true,
  install: false,
)
