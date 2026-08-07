config_validator_fs = import('fs')
jsoncons_dep = dependency(
  'jsoncons',
  required : true,
  fallback : ['jsoncons', 'jsoncons_dep'],
)

config_validator_schema_manifest = [
  ['urn:arm:pek:schema:model-descriptor:v1', 'config/schemas/v1/model.schema.json'],
  ['urn:arm:pek:schema:opchain-descriptor:v1', 'config/schemas/v1/opchain.schema.json'],
  ['urn:arm:pek:schema:opchain-common:v1', 'config/schemas/v1/opchain/common.schema.json'],
  ['urn:arm:pek:schema:opchain-op:v1', 'config/schemas/v1/opchain/op.schema.json'],
  ['urn:arm:pek:schema:inference-controller-op:v1', 'config/schemas/v1/opchain/ops/inference-controller.schema.json'],
  ['urn:arm:pek:schema:generic-image-preprocess-op:v1', 'config/schemas/v1/opchain/ops/generic-image-preprocess.schema.json'],
  ['urn:arm:pek:schema:inference-op:v1', 'config/schemas/v1/opchain/ops/inference.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-op:v1', 'config/schemas/v1/opchain/ops/generic-postprocess.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-camera-contact:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/camera-contact.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-dummy:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/dummy.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-gaze-detection:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/gaze-detection.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-imagenet-classification:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/imagenet-classification.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-modnet-segmentation:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/modnet-segmentation.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-object-embedding:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/object-embedding.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-paddle-ocr-detection:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/paddle-ocr-detection.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-person-classification:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/person-classification.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-rvm:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/rvm.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-scrfd:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/scrfd.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-ultraface:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/ultraface.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-yolo:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/yolo.schema.json'],
  ['urn:arm:pek:schema:generic-postprocess-parser-yolo-x:v1', 'config/schemas/v1/opchain/ops/generic-postprocess/yolo-x.schema.json'],
]

config_validator_embedded_schemas = ''
config_validator_raw_string_delimiter = 'PEKSCHEMA'
config_validator_raw_string_terminator = ')' + config_validator_raw_string_delimiter + '"'
foreach schema : config_validator_schema_manifest
  schema_id = schema[0]
  schema_path = schema[1]
  schema_text = config_validator_fs.read(meson.project_source_root() / '..' / schema_path)
  if schema_text.contains(config_validator_raw_string_terminator)
    error(schema_path + ' contains the embedded schema raw-string terminator')
  endif
  schema_entry = '    EmbeddedSchema{"' + schema_id + '", "' + schema_path + '", R"'
  schema_entry += config_validator_raw_string_delimiter + '(' + schema_text
  schema_entry += ')' + config_validator_raw_string_delimiter + '"},\n'
  config_validator_embedded_schemas += schema_entry
endforeach

config_validator_configuration = configuration_data()
config_validator_configuration.set('EMBEDDED_SCHEMAS', config_validator_embedded_schemas)
configure_file(
  input : 'EmbeddedSchemas.h.in',
  output : 'EmbeddedSchemas.h',
  configuration : config_validator_configuration,
)

config_validator_include = include_directories('.')
config_validator_public_deps = [
  glib_dep,
  fmt_dep,
  magic_enum_dep,
  nlohmann_dep,
  tl_expected_dep,
]

config_validator = static_library(
  'pek-config-validator',
  [
    'DocumentValidator.cpp',
    'Validator.cpp',
    'ValidationReport.cpp',
    'SchemaBundle.cpp',
    'ModelV1.cpp',
    'OpChainV1.cpp',
  ],
  include_directories : [config_validator_include, common_include],
  dependencies : [jsoncons_dep] + config_validator_public_deps,
  install : false,
)

config_validator_dep = declare_dependency(
  include_directories : [config_validator_include, common_include],
  dependencies : config_validator_public_deps,
  link_with : config_validator,
)
