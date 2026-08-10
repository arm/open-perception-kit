
asio_dep = dependency('asio', required: true,
                             fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])
openssl_dep = dependency('openssl', required : true)

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
  dependencies: [pek_deps, asio_dep, websocketpp_dep, openssl_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../pek',
)
