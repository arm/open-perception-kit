#pragma once

#include "amp/Result.h"
#include "amp/Types.h"

namespace amp {

struct TensorBuilder {

    struct ImageSource {

        const uint8_t *data = nullptr;
        size_t byteCount = 0;

        size_t width = 0;
        size_t height = 0;

        DataKind kind = DataKind::Unknown;
        amp::Tdt type = amp::Tdt::Float32;
    };

    struct ImageDestination {
        uint8_t *data = nullptr;
        size_t byteCount = 0;

        size_t width = 0;
        size_t height = 0;

        DataKind kind = DataKind::Unknown;
        amp::Tdt type = amp::Tdt::Float32;
    };

    struct AudioSource {};
    struct AudioDestination {};

    struct Setup {

        ImageSource imageSource;
        ImageDestination imageDestination;

        AudioSource audioSource;
        AudioDestination audioDestination;
    };

    virtual amp::Result<void> build(const TensorBuilder::Setup &setup) = 0;
};

} // namespace amp