#pragma once

#include <string>

//struct PerformanceTools {

    //static inline 
    uint64_t getNanos(void);

//};

struct PerformanceBlock {

    PerformanceBlock() {

    }

//    PerformanceBlock

  //  uint64_t startTime = PerformanceTools::getNanos();

};

struct PerformanceEntry {

    std::string name;
    size_t executionCount = 0;
    size_t allTimes = 0;

};

struct Performance {

  

};


