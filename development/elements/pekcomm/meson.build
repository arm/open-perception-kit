
asio_dep = dependency('asio', required: true,
                      fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])

pekcomm = shared_module(
  'pekcomm',                   
  sources: [
    'pekcomm.cpp',
    'writer.cpp',
    'file_writer.cpp',
    'websocket_writer.cpp',
  ],
  include_directories: [common_include],
  dependencies: [pek_deps, dependency('zlib'), asio_dep, websocketpp_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)

