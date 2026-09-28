if deb_package
if is_disabler(executorchops)
  error('deb_package requires -Dexecutorch=enabled and a complete ExecuTorch SDK.')
endif
if not python_ops_enabled
  error('deb_package requires -Dpython_ops=enabled and the locked Python Ops runtime.')
endif

open_perception_kit_dir = get_option('open_perception_kit_dir')
if open_perception_kit_dir == ''
  error('deb_package requires -Dopen_perception_kit_dir=/path/to/a/verified/sdk/triplet.')
endif
if not open_perception_kit_dir.startswith('/')
  error('open_perception_kit_dir must be an absolute path.')
endif

deb_revision = get_option('deb_package_revision')
if deb_revision == ''
  error('deb_package_revision must not be empty.')
endif

if host_machine.cpu_family() == 'x86_64'
  deb_architecture = 'amd64'
  deb_multiarch = 'x86_64-linux-gnu'
elif host_machine.cpu_family() == 'aarch64'
  deb_architecture = 'arm64'
  deb_multiarch = 'aarch64-linux-gnu'
else
  error('opk-runtime Debian packages support only x86_64 and aarch64.')
endif

package_script = find_program(
  opk_repository_root / 'scripts/private/packaging/package-opk-runtime-deb.sh',
)
meson_program = find_program('meson')

opk_runtime_deb = custom_target(
  'opk-runtime-deb',
  output : '@0@_@1@-@2@_@3@.deb'.format(
    'opk-runtime',
    meson.project_version(),
    deb_revision,
    deb_architecture,
  ),
  command : [
    package_script,
    '--build-dir', meson.project_build_root(),
    '--meson', meson_program,
    '--repo-root', opk_repository_root,
    '--installed-prefix', get_option('prefix'),
    '--installed-libdir', get_option('libdir'),
    '--python-runtime', opk_python.full_path(),
    '--open-perception-kit-dir', open_perception_kit_dir,
    '--onnx-runtime', onnx_libdir / 'libonnxruntime.so.1.24.4',
    '--onnx-license-dir', onnx_root / 'share/doc/onnxruntime',
    '--executorch-license-dir', executorch_root / '..' / 'executorch-legal-documentation',
    '--flatbuffers-schema-dir', opk_repository_root / 'schemas/perception/metadata',
    '--version', meson.project_version(),
    '--revision', deb_revision,
    '--architecture', deb_architecture,
    '--multiarch', deb_multiarch,
    '--output', '@OUTPUT@',
  ],
  depends : [
    common,
    opk_runtime,
    stdops,
    onnxops,
    executorchops,
    python_ops,
    gstopkinfer,
    gstopktracker,
    gstopkperformance,
    gstopkosd,
    opkinferpre,
    opkcomm,
  ],
  build_by_default : false,
  build_always_stale : true,
  console : true,
)
endif

if demo_deb_package
if host_machine.cpu_family() == 'x86_64'
  demo_deb_architecture = 'amd64'
elif host_machine.cpu_family() == 'aarch64'
  demo_deb_architecture = 'arm64'
else
  error('opk-demo Debian packages support only x86_64 and aarch64.')
endif

demo_package_script = find_program(
  opk_repository_root / 'scripts/private/packaging/package-opk-demo-deb.sh',
)
demo_meson_program = find_program('meson')

opk_demo_deb = custom_target(
  'opk-demo-deb',
  output : '@0@_@1@-@2@_@3@.deb'.format(
    'opk-demo',
    meson.project_version(),
    get_option('deb_package_revision'),
    demo_deb_architecture,
  ),
  command : [
    demo_package_script,
    '--build-dir', meson.project_build_root(),
    '--meson', demo_meson_program,
    '--repo-root', opk_repository_root,
    '--installed-prefix', get_option('prefix'),
    '--version', meson.project_version(),
    '--revision', get_option('deb_package_revision'),
    '--architecture', demo_deb_architecture,
    '--output', '@OUTPUT@',
  ],
  depends : [opk_menu],
  build_by_default : false,
  build_always_stale : true,
  console : true,
)
endif
