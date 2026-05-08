/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "parser.hpp"

// Helper: compare expected tokens to ExecArgs storage
static void ExpectTokensEq(const ExecArgs &ea, const std::vector<std::string> &expected) {
    std::vector<std::string> e = {"gst-launch-1.0"};
    e.insert(e.end(), expected.begin(), expected.end());

    ASSERT_EQ(ea.storage.size(), e.size());
    ASSERT_EQ(ea.argv.size(), e.size() + 1);
    ASSERT_EQ(ea.argv.back(), nullptr);

    for (size_t i = 0; i < e.size(); ++i) {
        EXPECT_EQ(ea.storage[i], e[i]) << "token " << i;
        // ensure argv points at the same c-string content
        EXPECT_STREQ(ea.argv[i], e[i].c_str()) << "argv token " << i;
    }
}

static void SetEnv(const char *k, const char *v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

static void UnsetEnv(const char *k) {
#ifdef _WIN32
    _putenv_s(k, "");
#else
    unsetenv(k);
#endif
}

TEST(TokenizeExpand, BasicWhitespaceSplit) {
    ExecArgs ea = tokenize_and_expand_argv("fakesrc ! fakesink");
    ExpectTokensEq(ea, {"fakesrc", "!", "fakesink"});
}

TEST(TokenizeExpand, DoubleQuotesKeepSpacesAndRemoveQuotes) {
    ExecArgs ea = tokenize_and_expand_argv(R"(v4l2src "device=/dev/video4" do-timestamp=true)");
    ExpectTokensEq(ea, {"v4l2src", "device=/dev/video4", "do-timestamp=true"});
}

TEST(TokenizeExpand, SingleQuotesKeepSpacesAndRemoveQuotes) {
    ExecArgs ea = tokenize_and_expand_argv("filesrc location='my file.mp4' ! fakesink");
    ExpectTokensEq(ea, {"filesrc", "location=my file.mp4", "!", "fakesink"});
}

TEST(TokenizeExpand, BackslashEscapesQuotes) {
    ExecArgs ea = tokenize_and_expand_argv(R"(a \"b c\" d)");
    // Your tokenizer treats backslash as "take next char literally" everywhere,
    // so \" becomes " and does not start a quote: token is `"b` etc.
    // If your implementation differs, adjust expected.
    ExpectTokensEq(ea, {"a", "\"b", "c\"", "d"});
}

TEST(TokenizeExpand, EnvExpansionNoResplitOnSpaces) {
    SetEnv("CAM0", "/dev/video 0"); // contains a space
    ExecArgs ea = tokenize_and_expand_argv("v4l2src device=${CAM0} do-timestamp=true");
    ExpectTokensEq(ea, {"v4l2src", "device=/dev/video 0", "do-timestamp=true"});
    UnsetEnv("CAM0");
}

TEST(TokenizeExpand, EnvDefaultValueUsedWhenUnsetOrEmpty) {
    UnsetEnv("CAM1");
    ExecArgs ea1 = tokenize_and_expand_argv("v4l2src device=${CAM1:-/dev/video4}");
    ExpectTokensEq(ea1, {"v4l2src", "device=/dev/video4"});

    SetEnv("CAM1", "");
    ExecArgs ea2 = tokenize_and_expand_argv("v4l2src device=${CAM1:-/dev/video7}");
    ExpectTokensEq(ea2, {"v4l2src", "device=/dev/video7"});

    SetEnv("CAM1", "/dev/video9");
    ExecArgs ea3 = tokenize_and_expand_argv("v4l2src device=${CAM1:-/dev/video7}");
    ExpectTokensEq(ea3, {"v4l2src", "device=/dev/video9"});
    UnsetEnv("CAM1");
}

TEST(TokenizeExpand, EnvErrorFormThrowsOnUnsetOrEmpty) {
    UnsetEnv("REQ");
    EXPECT_THROW(tokenize_and_expand_argv("x ${REQ?REQ is required} y"), std::runtime_error);

    SetEnv("REQ", "");
    EXPECT_THROW(tokenize_and_expand_argv("x ${REQ?REQ is required} y"), std::runtime_error);

    SetEnv("REQ", "ok");
    EXPECT_NO_THROW(tokenize_and_expand_argv("x ${REQ?REQ is required} y"));
    UnsetEnv("REQ");
}

TEST(TokenizeExpand, UnterminatedQuoteThrows) {
    EXPECT_THROW(tokenize_and_expand_argv(R"(a "b c)"), std::runtime_error);
    EXPECT_THROW(tokenize_and_expand_argv("a 'b c"), std::runtime_error);
}
