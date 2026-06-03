# Copyright (C) 2025 Arm Limited. All rights reserved.
set(FIXTURE_SOURCES src/main.c src/helper.c src/feature.c)

function(configure_fixture_target target_name)
    target_sources(${target_name} PRIVATE ${FIXTURE_SOURCES})
    target_compile_definitions(${target_name} PRIVATE FIXTURE_ENABLED=1)
endfunction()
