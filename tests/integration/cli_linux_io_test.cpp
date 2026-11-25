#include "cli/file_reader.hpp"
#include "cli_test_files.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace minisearch::test {
namespace {

using cli::FileError;
using cli::read_document_file;
using cli::test::TestFiles;

TEST(CliLinuxIOIntegration, RejectsDanglingSymlink) {
    const TestFiles files;
    const auto path = files.directory() / "dangling";
    std::error_code error;
    std::filesystem::create_symlink("unavailable-target", path, error);
    if (error) {
        GTEST_SKIP() << "Cannot create a symbolic link: " << error.message();
    }
    EXPECT_THROW(static_cast<void>(read_document_file(path.string())), FileError);
}

TEST(CliLinuxIOIntegration, RejectsUnixSocketWithoutReadingIt) {
    const TestFiles files;
    const auto path = (files.directory() / "socket").string();
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path)) {
        GTEST_SKIP() << "Temporary path does not fit sockaddr_un";
    }
    const int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        GTEST_SKIP() << "Unix sockets are unavailable (errno " << errno << ')';
    }
    address.sun_family = AF_UNIX;
    std::ranges::copy(path, address.sun_path);
    const auto bound =
        bind(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    const auto bind_error = errno;
    close(descriptor);
    if (bound != 0 && (bind_error == EACCES || bind_error == EPERM)) {
        GTEST_SKIP() << "Unix socket binding is restricted";
    }
    ASSERT_EQ(bound, 0) << "errno " << bind_error;
    ASSERT_TRUE(std::filesystem::is_socket(path));
    EXPECT_THROW(static_cast<void>(read_document_file(path)), FileError);
}

TEST(CliLinuxIOIntegration, ReportsRealPermissionDenialForARegularFile) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "Root can bypass read permissions";
    }
    const TestFiles files;
    const auto path = files.write("unreadable file", "document");
    std::filesystem::permissions(path, std::filesystem::perms::none);
    if (access(path.c_str(), R_OK) == 0) {
        GTEST_SKIP() << "The filesystem or process bypasses read permissions";
    }
    ASSERT_EQ(errno, EACCES);
    try {
        static_cast<void>(read_document_file(path.string()));
        FAIL() << "Unreadable file was accepted";
    } catch (const FileError &error) {
        EXPECT_TRUE(std::string_view(error.what()).starts_with("Cannot open file:"));
    }
}

} // namespace
} // namespace minisearch::test
