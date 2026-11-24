#include "cli/file_reader.hpp"
#include "cli_test_files.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <ios>
#include <sstream>
#include <streambuf>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace {

using minisearch::cli::FileError;
using minisearch::cli::max_file_bytes;
using minisearch::cli::read_document_bytes;
using minisearch::cli::read_document_file;
using minisearch::cli::test::TestFiles;

TEST(CliFileReaderTest, ReadsRelativeAbsoluteSpaceAndUnicodePathsInBinaryMode) {
    const TestFiles files(".");
    const std::string contents("alpha\r\nbeta\ngamma\0delta", 23);
    const auto path = files.write("file name.txt", contents);
    EXPECT_FALSE(path.is_absolute());
    EXPECT_EQ(read_document_file(path.string()), contents);
    EXPECT_EQ(read_document_file(std::filesystem::absolute(path).string()), contents);
    const auto unicode = files.write(std::filesystem::path(u8"belge-ö.txt"), "Hello");
    EXPECT_EQ(read_document_file(unicode.string()), "Hello");
    EXPECT_TRUE(read_document_file(files.write("empty.txt", "").string()).empty());
    const auto high = files.write("high.txt", "a\xFF");
    EXPECT_EQ(read_document_file(high.string()), "a\xFF");
}

TEST(CliFileReaderTest, RejectsMissingDirectoriesAndEmbeddedNulPaths) {
    const TestFiles files;
    EXPECT_THROW(static_cast<void>(read_document_file((files.directory() / "missing").string())),
                 FileError);
    EXPECT_THROW(static_cast<void>(read_document_file(files.directory().string())), FileError);
    const auto path = files.write("doc.txt", "hello").string() + std::string("\0suffix", 7);
    EXPECT_THROW(static_cast<void>(read_document_file(path)), FileError);
}

TEST(CliFileReaderTest, AcceptsRegularSymlinkTargetsAndRejectsDirectoryTargets) {
    const TestFiles files;
    const auto path = files.write("original.txt", "hi");
    const auto link = files.directory() / "linked.txt";
    std::error_code error;
    std::filesystem::create_symlink(path, link, error);
    if (error) {
        GTEST_SKIP() << "File system does not allow symlink creation";
    }
    EXPECT_EQ(read_document_file(link.string()), "hi");
    const auto directory_link = files.directory() / "directory-link";
    std::filesystem::create_directory_symlink(files.directory(), directory_link);
    EXPECT_THROW(static_cast<void>(read_document_file(directory_link.string())), FileError);
}

#if defined(__unix__) || defined(__APPLE__)
TEST(CliFileReaderTest, RejectsFifosBeforeOpeningThem) {
    const TestFiles files;
    const auto fifo = files.directory() / "pipe";
    ASSERT_EQ(mkfifo(fifo.c_str(), S_IRUSR | S_IWUSR), 0);
    EXPECT_THROW(static_cast<void>(read_document_file(fifo.string())), FileError);
}
#endif

TEST(CliFileReaderTest, EnforcesFileAndActualReadLimitsAndDetectsSizeChanges) {
    const TestFiles files;
    const auto path = files.write("limit.txt", "");
    std::filesystem::resize_file(path, max_file_bytes);
    EXPECT_EQ(read_document_file(path.string()).size(), max_file_bytes);
    std::filesystem::resize_file(path, max_file_bytes + 1);
    EXPECT_THROW(static_cast<void>(read_document_file(path.string())), FileError);

    std::istringstream oversized(std::string(max_file_bytes + 1, 'x'));
    EXPECT_THROW(static_cast<void>(read_document_bytes(oversized, max_file_bytes)), FileError);
    std::istringstream shrunk("short");
    EXPECT_THROW(static_cast<void>(read_document_bytes(shrunk, 6)), FileError);
    std::istringstream grown("expanded");
    EXPECT_THROW(static_cast<void>(read_document_bytes(grown, 3)), FileError);
}

class FailingFileInput : public std::streambuf {
  public:
    FailingFileInput() { setg(prefix_, prefix_, prefix_ + 2); }

  protected:
    int_type underflow() override { throw std::ios_base::failure("read failed"); }

  private:
    char prefix_[2]{'a', 'b'};
};

TEST(CliFileReaderTest, RejectsPartialIoFailuresAndAcceptsNormalEofWithExceptionMasks) {
    for (const auto mask : {std::ios::goodbit, std::ios::badbit}) {
        FailingFileInput buffer;
        std::istream input(&buffer);
        input.exceptions(mask);
        EXPECT_THROW(static_cast<void>(read_document_bytes(input, 2)), FileError);
    }
    std::istringstream input("complete");
    input.exceptions(std::ios::eofbit | std::ios::failbit | std::ios::badbit);
    EXPECT_EQ(read_document_bytes(input, 8), "complete");

    std::istringstream failed;
    failed.setstate(std::ios::failbit);
    EXPECT_THROW(static_cast<void>(read_document_bytes(failed, 0)), FileError);
}

} // namespace
