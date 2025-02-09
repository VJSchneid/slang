// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "Test.h"

#include "slang/text/Glob.h"
#include "slang/text/SourceManager.h"
#include "slang/util/String.h"

std::string getTestInclude() {
    return findTestDir() + "/include.svh";
}

TEST_CASE("Read source") {
    SourceManager manager;
    std::string testPath = manager.makeAbsolutePath(getTestInclude());

    CHECK(!manager.readSource("X:\\nonsense.txt", /* library */ nullptr));

    auto file = manager.readSource(testPath, /* library */ nullptr);
    REQUIRE(file);
    CHECK(file.data.length() > 0);
}

TEST_CASE("Read header (absolute)") {
    SourceManager manager;
    std::string testPath = manager.makeAbsolutePath(getTestInclude());

    // check load failure
    CHECK(!manager.readHeader("X:\\nonsense.txt", SourceLocation(), nullptr, false));

    // successful load
    SourceBuffer buffer = manager.readHeader(testPath, SourceLocation(), nullptr, false);
    REQUIRE(buffer);
    CHECK(!buffer.data.empty());

    // next load should be cached
    buffer = manager.readHeader(testPath, SourceLocation(), nullptr, false);
    CHECK(!buffer.data.empty());
}

TEST_CASE("Read header (relative)") {
    SourceManager manager;

    // relative to nothing should never return anything
    CHECK(!manager.readHeader("relative", SourceLocation(), nullptr, false));

    // get a file ID to load relative to
    SourceBuffer buffer1 = manager.readHeader(manager.makeAbsolutePath(getTestInclude()),
                                              SourceLocation(), nullptr, false);
    REQUIRE(buffer1);

    // reading the same header by name should return the same ID
    SourceBuffer buffer2 = manager.readHeader("include.svh", SourceLocation(buffer1.id, 0), nullptr,
                                              false);

    // should be able to load relative
    buffer2 = manager.readHeader("nested/file.svh", SourceLocation(buffer1.id, 0), nullptr, false);
    REQUIRE(buffer2);
    CHECK(!buffer2.data.empty());

    // load another level of relative
    CHECK(manager.readHeader("nested_local.svh", SourceLocation(buffer2.id, 0), nullptr, false));
}

TEST_CASE("Read header (include dirs)") {
    SourceManager manager;
    CHECK(manager.addSystemDirectory(manager.makeAbsolutePath(findTestDir())));

    SourceBuffer buffer = manager.readHeader("include.svh", SourceLocation(), nullptr, true);
    REQUIRE(buffer);

    CHECK(manager.addUserDirectory(manager.makeAbsolutePath(findTestDir() + "/nested")));
    buffer = manager.readHeader("../infinite_chain.svh", SourceLocation(buffer.id, 0), nullptr,
                                false);
    CHECK(buffer);
}

TEST_CASE("Read header (dev/null)") {
    if (fs::exists("/dev/null")) {
        SourceManager manager;
        SourceBuffer buffer = manager.readHeader("/dev/null", SourceLocation(), nullptr, true);
        CHECK(buffer);
    }
}

static void globAndCheck(const fs::path& basePath, std::string_view pattern, GlobMode mode,
                         GlobRank expectedRank, std::initializer_list<const char*> expected) {
    SmallVector<fs::path> results;
    auto rank = svGlob(basePath, pattern, mode, results);

    CHECK(rank == expectedRank);
    CHECK(results.size() == expected.size());
    for (auto str : expected) {
        auto it = std::ranges::find_if(results, [str, mode](auto& item) {
            return mode == GlobMode::Files ? item.filename() == str
                                           : item.parent_path().filename() == str;
        });
        if (it == results.end()) {
            FAIL_CHECK(str << " is not found in results for " << pattern);
        }
    }

    for (auto& path : results) {
        if (mode == GlobMode::Files)
            CHECK(fs::is_regular_file(path));
        else
            CHECK(fs::is_directory(path));
    }
}

TEST_CASE("File globbing") {
    auto testDir = findTestDir();
    globAndCheck(testDir, "*st?.sv", GlobMode::Files, GlobRank::WildcardName,
                 {"test2.sv", "test3.sv", "test4.sv", "test5.sv", "test6.sv"});
    globAndCheck(testDir, "system", GlobMode::Files, GlobRank::ExactName, {});
    globAndCheck(testDir, "system/", GlobMode::Files, GlobRank::Directory, {"system.svh"});
    globAndCheck(testDir, ".../f*.svh", GlobMode::Files, GlobRank::WildcardName,
                 {"file.svh", "file_defn.svh", "file_uses_defn.svh"});
    globAndCheck(testDir, "*ste*/", GlobMode::Files, GlobRank::Directory,
                 {"file.svh", "macro.svh", "nested_local.svh", "system.svh"});
    globAndCheck(testDir, testDir + "/library/pkg.sv", GlobMode::Files, GlobRank::ExactName,
                 {"pkg.sv"});
    globAndCheck(testDir, "*??blah", GlobMode::Files, GlobRank::WildcardName, {});

    putenv((char*)"BAR#=cmd");
    globAndCheck(testDir, "*${BAR#}.f", GlobMode::Files, GlobRank::WildcardName, {"cmd.f"});
}

TEST_CASE("Directory globbing") {
    auto testDir = findTestDir();
    globAndCheck(testDir, "*st?.sv", GlobMode::Directories, GlobRank::WildcardName, {});
    globAndCheck(testDir, "system", GlobMode::Directories, GlobRank::ExactName, {"system"});
    globAndCheck(testDir, "system/", GlobMode::Directories, GlobRank::Directory, {"system"});
    globAndCheck(testDir, ".../", GlobMode::Directories, GlobRank::Directory,
                 {"library", "nested", "system", "data"});
}

TEST_CASE("Config Blocks") {
    auto tree = SyntaxTree::fromText(R"(
module m;
endmodule

config cfg1;
    localparam S = 24;

    design rtlLib.top;
    default liblist rtlLib;
    instance top.a2 liblist gateLib;
endconfig
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}
