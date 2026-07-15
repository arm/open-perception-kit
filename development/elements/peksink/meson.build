
asio_dep = dependency('asio', required: true,
                             fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])
openssl_dep = dependency('openssl', required : true)

peksink_static_files_dir = meson.project_source_root() / 'web/content'
peksink_shared_files_dir = meson.project_source_root() / 'web/shared'
peksink_cpp_args = [
  '-DPEK_DEFAULT_STATIC_FILES_LOCATION="' + peksink_static_files_dir + '"',
  '-DPEK_SHARED_STATIC_FILES_LOCATION="' + peksink_shared_files_dir + '"'
]

pekinferpre = shared_module(
  'peksink',                   
  sources: [
    'peksink.cpp',
    'http_server.cpp',
    'webrtc_session.cpp',
    'webrtc_ws.cpp',
    'ctrl_ws.cpp',
    'utils.cpp',
    'model_reg.cpp'
  ],
  include_directories: [common_include],
  cpp_args: peksink_cpp_args,
  dependencies: [pek_deps, asio_dep, websocketpp_dep, openssl_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)
