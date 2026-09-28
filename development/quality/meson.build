# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

# Analysis uses configured headers and the compilation database, not object files.
foreach scope : ['all', 'new']
  run_target(
    'clang-tidy-' + scope,
    command : [
      find_program('python3', native : true),
      opk_repository_root / 'scripts/private/clang-tidy-report.py',
      '--repository', opk_repository_root,
      '--build-directory', meson.project_build_root(),
      '--scope', scope,
    ],
  )
endforeach
