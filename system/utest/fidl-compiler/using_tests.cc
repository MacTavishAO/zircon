// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

bool valid_using() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

struct Foo {
    dependent.Bar dep;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool valid_using_with_as_refs_through_both() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
    the_alias.Bar dep2;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool valid_using_with_as_ref_only_through_fqn() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool valid_using_with_as_ref_only_through_alias() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    the_alias.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool invalid_missing_using() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

// missing using.

struct Foo {
    dependent.Bar dep;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnknownType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent.Bar");

  END_TEST;
}

bool invalid_unknown_using() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

using dependent; // unknown using.

struct Foo {
    dependent.Bar dep;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnknownLibrary);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent");

  END_TEST;
}

bool invalid_duplicate_using() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;
using dependent; // duplicated

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDuplicateLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent");

  END_TEST;
}

bool invalid_unused_using() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

struct Foo {
    int64 does_not;
    int32 use_dependent;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnusedImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent");

  END_TEST;
}

bool invalid_unknown_dependent_library() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

const foo.bar.baz QUX = 0;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnknownDependentLibrary);

  END_TEST;
}

bool invalid_too_many_provided_libraries() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;

  TestLibrary dependency("notused.fidl", "library not.used;", &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", "library example;", &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  auto unused = shared.all_libraries.Unused(library.library());
  ASSERT_EQ(1, unused.size());
  ASSERT_STR_EQ("not.used", fidl::NameLibrary(*unused.begin()).c_str());

  END_TEST;
}

bool files_disagree_on_library_name() {
  BEGIN_TEST;

  TestLibrary library("lib_file1.fidl",
                      R"FIDL(
library lib;
)FIDL");
  library.AddSource("lib_file2.fidl",
                    R"FIDL(
library dib;
  )FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrFilesDisagreeOnLibraryName);

  END_TEST;
}

bool library_declaration_name_collision() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());
  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep;

struct dep{};

struct B{dep.A a;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dep");
  END_TEST;
}

bool aliased_library_declaration_name_collision() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());
  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as x;

struct x{};

struct B{dep.A a;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "x");
  END_TEST;
}

bool aliased_library_nonaliased_declaration_name_collision() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());
  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as depnoconflict;

struct dep{};

struct B{depnoconflict.A a;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dep");
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(using_tests)
RUN_TEST(valid_using)
RUN_TEST(valid_using_with_as_refs_through_both)
RUN_TEST(valid_using_with_as_ref_only_through_fqn)
RUN_TEST(valid_using_with_as_ref_only_through_alias)
RUN_TEST(invalid_missing_using)
RUN_TEST(invalid_unknown_using)
RUN_TEST(invalid_duplicate_using)
RUN_TEST(invalid_unused_using)
RUN_TEST(invalid_unknown_dependent_library)
RUN_TEST(invalid_too_many_provided_libraries)
RUN_TEST(files_disagree_on_library_name)
RUN_TEST(library_declaration_name_collision)
RUN_TEST(aliased_library_declaration_name_collision)
RUN_TEST(aliased_library_nonaliased_declaration_name_collision)
END_TEST_CASE(using_tests)
