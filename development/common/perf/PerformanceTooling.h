/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <string>
#include <thread>

namespace amp {

struct Streamline {

    struct Channel {
        constexpr static int Preprocess = 1;
        constexpr static int Inference = 2;
        constexpr static int Postprocess = 3;
    };

    static void setup();
    static void start(int channelId, const std::string &annotation);
    static void end(int channelId);

  protected:
    static void createChannel(int channelId, int groupId, const std::string &channelName);

  public:
    struct Scooped {
        Scooped(const std::string &name) : name(name) {
            Streamline::start(1, name);
        }

        ~Scooped() {
            Streamline::end(1);
        }
        std::string name;
    };
};

} // namespace amp
