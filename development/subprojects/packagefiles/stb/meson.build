project('stb', 'c')

stb_inc = include_directories('.')

stb_dep = declare_dependency(
  include_directories: stb_inc
)

meson.override_dependency('stb', stb_dep)