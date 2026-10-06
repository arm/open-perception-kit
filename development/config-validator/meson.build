# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

config_validator_fs = import('fs')
jsoncons_dep = dependency(
  'jsoncons',
  required : true,
  fallback : ['jsoncons', 'jsoncons_dep'],
)

config_validator_schema_manifest = [
  ['urn:arm:opk:schema:pipeline-descriptor:v1', 'config/schemas/v1/pipeline.schema.json'],
  ['urn:arm:opk:schema:model-descriptor:v1', 'config/schemas/v1/model.schema.json'],
  ['urn:arm:opk:schema:opchain-descriptor:v1', 'config/schemas/v1/opchain.schema.json'],
  ['urn:arm:opk:schema:opchain-common:v1', 'config/schemas/v1/opchain/common.schema.json'],
  ['urn:arm:opk:schema:opchain-op:v1', 'config/schemas/v1/opchain/op.schema.json'],
  ['urn:arm:opk:schema:inference-controller-op:v1', 'config/schemas/v1/opchain/ops/inference-controller.schema.json'],
  ['urn:arm:opk:schema:generic-image-preprocess-op:v1', 'config/schemas/v1/opchain/ops/generic-image-preprocess.schema.json'],
  ['urn:arm:opk:schema:inference-op:v1', 'config/schemas/v1/opchain/ops/inference.schema.json'],
  ['urn:arm:opk:schema:generic-postprocess-op:v1', 'config/schemas/v1/opchain/ops/generic-postprocess.schema.json'],
  ['urn:arm:opk:schema:python-script-op:v1', 'config/schemas/v1/opchain/ops/python-script.schema.json'],
]

config_validator_embedded_schemas = ''
config_validator_raw_string_delimiter = 'OPKSCHEMA'
config_validator_raw_string_terminator = ')' + config_validator_raw_string_delimiter + '"'
foreach schema : config_validator_schema_manifest
  schema_id = schema[0]
  schema_path = schema[1]
  schema_text = config_validator_fs.read(opk_repository_root / schema_path)
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
  'opk-config-validator',
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
  link_with : [config_validator, logging],
)

opk_config_check = executable(
  'opk-config-check',
  ['opk-config-check.cpp', 'RepositoryValidator.cpp'],
  cpp_args : ['-DFMT_HEADER_ONLY'],
  dependencies : [config_validator_dep, jsoncons_dep],
  link_whole : [logging],
  install : false,
)
