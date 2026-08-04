/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "perf/PerformanceTracer.h"

TEST(PerformanceTracer, DoesNotRetainCurrentCycleMeasurementsWithoutConsumer) {
    pek::perf::PerformanceTracer tracer;

    for (int i = 0; i < 2000; ++i) {
        tracer.start("test");
        tracer.end("test");
    }

    EXPECT_TRUE(tracer.getCurrentCycleMeasurements().empty());

    tracer.registerCurrentCycleConsumer();
    tracer.registerCurrentCycleConsumer();
    tracer.start("test");
    tracer.end("test");
    EXPECT_EQ(tracer.getCurrentCycleMeasurements().size(), 1);

    tracer.unregisterCurrentCycleConsumer();
    EXPECT_EQ(tracer.getCurrentCycleMeasurements().size(), 1);

    tracer.unregisterCurrentCycleConsumer();
    EXPECT_TRUE(tracer.getCurrentCycleMeasurements().empty());
}
