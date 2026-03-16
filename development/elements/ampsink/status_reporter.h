/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __STATUS_REPORTER_H__
#define __STATUS_REPORTER_H__

#include <nlohmann/json.hpp>

class StatusReporter {
  protected:
    friend class CtrlWebSocket;

    // shall be called when the reporter has something to report
    // by default it does nothing
    // gets its real value when this reporter gets registered to CtrlWebSocket
    std::function<void()> trigger_reporting = []() {};

  public:
    virtual nlohmann::json report() const = 0;
};

#endif // !__STATUS_REPORTER_H__
