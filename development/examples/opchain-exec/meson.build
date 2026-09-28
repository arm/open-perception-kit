# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

opchain_exec = executable(
  'opchain-exec',
  'main.cpp',
  example_common_sources,
  build_rpath: '$ORIGIN/',
  include_directories: [example_common_include, common_include, runtime_include],
  dependencies: [fmt_dep, magic_enum_dep, nlohmann_dep, tl_expected_dep, open_perception_kit_dep],
  link_with: [opk_runtime, common],
  install: false,
)
