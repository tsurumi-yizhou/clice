#pragma once

#include "AST/SourceLocation.h"
#include "clang/Lex/MacroInfo.h"
#include "llvm/ADT/DenseMap.h"

namespace clice {

/// Information about `#include` directive.
struct Include {
    /// The file id of the included file. If the file is skipped because of
    /// include guard, or `#pragma once`, this will be invalid.
    clang::FileID fid;

    /// Location of the `include`.
    clang::SourceLocation location;

    /// The name of the included file.
    std::string fileName;

    /// If the file was found via an absolute include path, `searchPath` will be empty.
    /// For framework includes, the `searchPath` and `relativePath` will be split up.
    ///
    /// For example, if an include of "Some/Some.h" is found via the framework path
    /// "path/to/Frameworks/Some.framework/Headers/Some.h"
    /// `searchPath` will be "path/to/Frameworks/Some.framework/Headers"
    /// `relativePath` will be "Some.h".
    std::string searchPath;

    /// See `searchPath`.
    std::string relativePath;
};

/// Information about `__has_include` directive.
struct HasInclude {
    /// The path of the included file.
    llvm::StringRef path;

    /// Location of the filename token start.
    clang::SourceLocation location;
};

/// Information about `#if`, `#ifdef`, `#ifndef`, `#elif`,
/// `#elifdef`, `#else`, `#endif` directive.
struct Condition {
    enum class BranchKind : uint8_t {
        If = 0,
        Elif,
        Ifdef,
        Elifdef,
        Ifndef,
        Elifndef,
        Else,
        EndIf,
    };

    using enum BranchKind;

    enum class ConditionValue : uint8_t {
        True = 0,
        False,
        Skipped,
        None,
    };

    using enum ConditionValue;

    /// Kind of the branch.
    BranchKind kind;

    /// Value of the condition.
    ConditionValue value;

    /// Location of the directive identifier.
    clang::SourceLocation loc;

    /// Range of the condition.
    clang::SourceRange conditionRange;
};

/// Information about macro definition, reference and undef.
struct MacroRef {
    enum class Kind : uint8_t {
        Def = 0,
        Ref,
        Undef,
    };

    using enum Kind;

    /// The macro definition information.
    const clang::MacroInfo* macro;

    /// Kind of the macro reference.
    Kind kind;

    /// The location of the macro name.
    clang::SourceLocation loc;
};

/// Information about `#pragma` directive.
struct Pragma {
    enum class Kind : uint8_t {
        Region,
        EndRegion,

        // Other unused cases in clice, For example: `#pragma once`.
        Other,
    };

    using enum Kind;

    /// The pragma text in that line, for example:
    ///     "#pragma region"
    ///     "#pragma once"
    ///     "#pragma GCC error"
    llvm::StringRef stmt;

    /// Kind of the pragma.
    Kind kind;

    /// Location of the pragma token.
    clang::SourceLocation loc;
};

struct Directive {
    std::vector<Include> includes;
    std::vector<HasInclude> hasIncludes;
    std::vector<Condition> conditions;
    std::vector<MacroRef> macros;
    std::vector<Pragma> pragmas;

    /// Tell preprocessor to collect directives information and store them in `directives`.
    static void attach(clang::Preprocessor& pp,
                       llvm::DenseMap<clang::FileID, Directive>& directives);
};

}  // namespace clice
