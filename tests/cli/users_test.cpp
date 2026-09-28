#include "mq/cli/users.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

using mq::cli::UserEntry;

// Collects every user handed to the callback so tests can assert on it.
class Recorder {
public:
    // accept=false simulates the caller rejecting an entry.
    explicit Recorder(bool accept = true) : accept_(accept) {}

    bool onAdd(const UserEntry& user) {
        seen.push_back(user);
        return accept_;
    }

    mq::cli::AddUserFn fn() {
        return [this](const UserEntry& user) { return onAdd(user); };
    }

    std::vector<UserEntry> seen;

private:
    bool accept_;
};

// True when any error line contains needle.
bool hasError(const mq::cli::UsersParseResult& result, const std::string& needle) {
    for (const std::string& line : result.errors) {
        if (line.find(needle) != std::string::npos) return true;
    }

    return false;
}

TEST(UsersTest, AcceptsSingleEntry) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:secret:/alice", recorder.fn());

    EXPECT_EQ(result.accepted, 1U);
    EXPECT_EQ(result.rejected, 0U);
    EXPECT_TRUE(result.errors.empty());
    ASSERT_EQ(recorder.seen.size(), 1U);
    EXPECT_EQ(recorder.seen[0].name, "alice");
    EXPECT_EQ(recorder.seen[0].password, "secret");
    EXPECT_EQ(recorder.seen[0].vhost, "/alice");
}

TEST(UsersTest, AcceptsMultipleEntries) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:pw:/a;bob:pw2:/b", recorder.fn());

    EXPECT_EQ(result.accepted, 2U);
    EXPECT_EQ(result.rejected, 0U);
    ASSERT_EQ(recorder.seen.size(), 2U);
    EXPECT_EQ(recorder.seen[0].name, "alice");
    EXPECT_EQ(recorder.seen[1].name, "bob");
    EXPECT_EQ(recorder.seen[1].vhost, "/b");
}

TEST(UsersTest, SkipsEmptyEntriesFromExtraSemicolons) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec(";;alice:pw:/a;;", recorder.fn());

    EXPECT_EQ(result.accepted, 1U);
    EXPECT_EQ(result.rejected, 0U);
    EXPECT_TRUE(result.errors.empty());
    EXPECT_EQ(recorder.seen.size(), 1U);
}

TEST(UsersTest, EmptySpecIsANoop) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("", recorder.fn());

    EXPECT_EQ(result.accepted, 0U);
    EXPECT_EQ(result.rejected, 0U);
    EXPECT_TRUE(recorder.seen.empty());
}

TEST(UsersTest, RejectsEntryWithMissingFields) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:secret", recorder.fn());

    EXPECT_EQ(result.accepted, 0U);
    EXPECT_EQ(result.rejected, 1U);
    EXPECT_TRUE(recorder.seen.empty());
    EXPECT_TRUE(hasError(result, "user:password:vhost"));
}

TEST(UsersTest, RejectsEmptyUserName) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec(":secret:/a", recorder.fn());

    EXPECT_EQ(result.accepted, 0U);
    EXPECT_EQ(result.rejected, 1U);
    EXPECT_TRUE(hasError(result, "empty user name"));
}

TEST(UsersTest, RejectsEmptyVhost) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:secret:", recorder.fn());

    EXPECT_EQ(result.accepted, 0U);
    EXPECT_EQ(result.rejected, 1U);
    EXPECT_TRUE(hasError(result, "empty vhost"));
}

TEST(UsersTest, EmptyPasswordIsAcceptedWithWarning) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice::/a", recorder.fn());

    EXPECT_EQ(result.accepted, 1U);
    EXPECT_EQ(result.rejected, 0U);
    EXPECT_TRUE(hasError(result, "empty password"));
    ASSERT_EQ(recorder.seen.size(), 1U);
    EXPECT_EQ(recorder.seen[0].password, "");
}

// Only the first two ':' separate fields; later ones belong to the vhost.
TEST(UsersTest, ExtraColonBelongsToVhost) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:secret:/a:b", recorder.fn());

    ASSERT_EQ(recorder.seen.size(), 1U);
    EXPECT_EQ(recorder.seen[0].password, "secret");
    EXPECT_EQ(recorder.seen[0].vhost, "/a:b");
}

TEST(UsersTest, TrimsSurroundingWhitespace) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("  alice : secret : /a  ", recorder.fn());

    ASSERT_EQ(recorder.seen.size(), 1U);
    EXPECT_EQ(recorder.seen[0].name, "alice");
    EXPECT_EQ(recorder.seen[0].password, "secret");
    EXPECT_EQ(recorder.seen[0].vhost, "/a");
}

TEST(UsersTest, CallerRejectionCountsAsRejected) {
    Recorder recorder(/*accept=*/false);
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:secret:/a", recorder.fn());

    EXPECT_EQ(result.accepted, 0U);
    EXPECT_EQ(result.rejected, 1U);
    EXPECT_TRUE(hasError(result, "rejected by the caller"));
    EXPECT_EQ(recorder.seen.size(), 1U);  // the caller still saw the entry
}

TEST(UsersTest, GoodAndBadEntriesAreCountedSeparately) {
    Recorder recorder;
    const mq::cli::UsersParseResult result = mq::cli::parseUsersSpec(
        "alice:pw:/a;broken;bob:pw:/b", recorder.fn());

    EXPECT_EQ(result.accepted, 2U);
    EXPECT_EQ(result.rejected, 1U);
    EXPECT_EQ(recorder.seen.size(), 2U);
}

TEST(UsersTest, RejectsSameUserNameTwice) {
    Recorder recorder;
    const mq::cli::UsersParseResult result =
        mq::cli::parseUsersSpec("alice:pw:/a;alice:pw2:/b", recorder.fn());

    EXPECT_EQ(result.accepted, 1U);
    EXPECT_EQ(result.rejected, 1U);
    EXPECT_TRUE(hasError(result, "configured twice"));
    ASSERT_EQ(recorder.seen.size(), 1U);
    EXPECT_EQ(recorder.seen[0].vhost, "/a");
}

TEST(UsersTest, LoadsFromEnvironment) {
    ::setenv("MQ_USERS", "envuser:envpw:/env", 1);
    Recorder recorder;
    const mq::cli::UsersParseResult result = mq::cli::loadUsersFromEnv(recorder.fn());
    ::unsetenv("MQ_USERS");

    EXPECT_EQ(result.accepted, 1U);
    ASSERT_EQ(recorder.seen.size(), 1U);
    EXPECT_EQ(recorder.seen[0].name, "envuser");
}

TEST(UsersTest, MissingEnvironmentVariableIsANoop) {
    ::unsetenv("MQ_USERS");
    Recorder recorder;
    const mq::cli::UsersParseResult result = mq::cli::loadUsersFromEnv(recorder.fn());

    EXPECT_EQ(result.accepted, 0U);
    EXPECT_EQ(result.rejected, 0U);
    EXPECT_TRUE(recorder.seen.empty());
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
