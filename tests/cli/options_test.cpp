#include "mq/cli/options.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// Turns an initializer_list into the argv parseOptions expects.
class Args {
public:
    explicit Args(std::initializer_list<const char*> list) {
        storage_.emplace_back("amqp_server");  // argv[0]
        for (const char* item : list) storage_.emplace_back(item);
        for (std::string& item : storage_) argv_.push_back(item.data());
    }

    int argc() const { return static_cast<int>(argv_.size()); }
    char** argv() { return argv_.data(); }

private:
    std::vector<std::string> storage_;
    std::vector<char*> argv_;
};

mq::cli::ParseResult parse(std::initializer_list<const char*> list) {
    Args args(list);
    return mq::cli::parseOptions(args.argc(), args.argv());
}

// Parse then apply; writes to out only when both steps succeed.
bool parseAndApply(std::initializer_list<const char*> list,
                   mq::cli::ServerOptions* out, std::string* error) {
    const mq::cli::ParseResult parsed = parse(list);
    if (!parsed.ok) {
        if (error != nullptr) *error = parsed.error;
        return false;
    }

    return mq::cli::applyOptions(parsed, out, error);
}

}  // namespace

static_assert(mq::cli::kDefaultPort == 5672, "default port changed");
static_assert(mq::cli::kDefaultHeartbeat == 60, "default heartbeat changed");
static_assert(mq::cli::kDefaultHttpPort == 0, "default http port changed");
static_assert(mq::cli::kOptionSpecCount >= 5, "option table regressed");

TEST(OptionsTest, DefaultsWhenNoArguments) {
    const mq::cli::ParseResult parsed = parse({});
    EXPECT_TRUE(parsed.ok);
    EXPECT_FALSE(parsed.help_requested);
    EXPECT_EQ(parsed.count, 0u);

    mq::cli::ServerOptions options;
    std::string error;
    ASSERT_TRUE(mq::cli::applyOptions(parsed, &options, &error));
    EXPECT_EQ(options.port, mq::cli::kDefaultPort);
    EXPECT_EQ(options.heartbeat, mq::cli::kDefaultHeartbeat);
    EXPECT_EQ(options.http_port, mq::cli::kDefaultHttpPort);
    EXPECT_TRUE(options.data_dir.empty());
}

TEST(OptionsTest, ShortAndLongPortAreEquivalent) {
    mq::cli::ServerOptions short_form;
    mq::cli::ServerOptions long_form;
    std::string error;
    ASSERT_TRUE(parseAndApply({"-p", "1234"}, &short_form, &error)) << error;
    ASSERT_TRUE(parseAndApply({"--port", "1234"}, &long_form, &error)) << error;
    EXPECT_EQ(short_form.port, 1234);
    EXPECT_EQ(long_form.port, 1234);
}

TEST(OptionsTest, HeartbeatZeroMeansDisabledNotUnset) {
    mq::cli::ServerOptions options;
    std::string error;
    ASSERT_TRUE(parseAndApply({"--heartbeat", "0"}, &options, &error)) << error;
    EXPECT_EQ(options.heartbeat, 0);
}

TEST(OptionsTest, DataDirAndHttpPortAreApplied) {
    mq::cli::ServerOptions options;
    std::string error;
    ASSERT_TRUE(parseAndApply({"--data", "/tmp/mq-data", "--http-port", "15672"},
                              &options, &error))
        << error;
    EXPECT_EQ(options.data_dir, "/tmp/mq-data");
    EXPECT_EQ(options.http_port, 15672);
}

TEST(OptionsTest, ExplicitEmptyDataDirIsAccepted) {
    mq::cli::ServerOptions options;
    std::string error;
    ASSERT_TRUE(parseAndApply({"--data", ""}, &options, &error)) << error;
    EXPECT_TRUE(options.data_dir.empty());  // in-memory mode
}

TEST(OptionsTest, RejectsMalformedArguments) {
    mq::cli::ParseResult parsed = parse({"--foo"});
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.failed, mq::cli::OptionKind::kUnknown);
    EXPECT_NE(parsed.error.find("--foo"), std::string::npos);

    parsed = parse({"--data"});
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.failed, mq::cli::OptionKind::kMissingValue);
    EXPECT_NE(parsed.error.find("--data"), std::string::npos);

    parsed = parse({"foo"});
    EXPECT_FALSE(parsed.ok);
    EXPECT_NE(parsed.error.find("unexpected argument"), std::string::npos);

    mq::cli::ServerOptions options;
    std::string error;
    EXPECT_FALSE(parseAndApply({"--port", "abc"}, &options, &error));
    EXPECT_NE(error.find("--port"), std::string::npos);
    EXPECT_FALSE(parseAndApply({"--port", "70000"}, &options, &error));
    EXPECT_FALSE(parseAndApply({"--port", "0"}, &options, &error));
    EXPECT_FALSE(parseAndApply({"--port", "-1"}, &options, &error));
    EXPECT_FALSE(parseAndApply({"--heartbeat", "70000"}, &options, &error));
    EXPECT_FALSE(parseAndApply({"--http-port", "-1"}, &options, &error));
}

TEST(OptionsTest, HelpIsRequestedAndNotAnError) {
    for (const char* flag : {"-h", "--help"}) {
        const mq::cli::ParseResult parsed = parse({flag});
        EXPECT_TRUE(parsed.ok);
        EXPECT_TRUE(parsed.help_requested);
    }
}

TEST(OptionsTest, HelpWinsOverUnknownOption) {
    const mq::cli::ParseResult parsed = parse({"--help", "--foo"});
    EXPECT_FALSE(parsed.ok);  // --foo still fails the parse
    EXPECT_TRUE(parsed.help_requested);
}

TEST(OptionsTest, RepeatedOptionUsesLastValue) {
    mq::cli::ServerOptions options;
    std::string error;
    ASSERT_TRUE(parseAndApply({"-p", "1111", "-p", "2222"}, &options, &error))
        << error;
    EXPECT_EQ(options.port, 2222);
}

TEST(OptionsTest, TooManyOptionsFailWithoutOverflow) {
    std::vector<std::string> storage;
    std::vector<char*> argv;
    storage.emplace_back("amqp_server");
    for (std::size_t i = 0; i < mq::cli::kMaxParsedOptions + 5; ++i) {
        storage.emplace_back("--heartbeat");
        storage.emplace_back("1");
    }

    for (std::string& item : storage) argv.push_back(item.data());

    const mq::cli::ParseResult parsed =
        mq::cli::parseOptions(static_cast<int>(argv.size()), argv.data());
    EXPECT_FALSE(parsed.ok);
    EXPECT_NE(parsed.error.find("too many options"), std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
