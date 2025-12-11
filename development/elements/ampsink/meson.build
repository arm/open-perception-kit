
asio_dep = dependency('asio', required: true,
                             fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])
openssl_dep = dependency('openssl', required : true)

ampinferpre = shared_module(
  'ampsink',                   
  sources: [
    'ampsink.cpp'
  ],
  include_directories: [common_include],
  dependencies: [amp_deps, asio_dep, websocketpp_dep, openssl_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)

