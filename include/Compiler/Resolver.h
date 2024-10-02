#pragma once

#include "ParsedAST.h"
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Template.h>
#include <stack>

namespace clice {

class TemplateResolver {
public:
    TemplateResolver(clang::Sema& sema) : sema(sema) {}

    clang::QualType resolve(clang::QualType type);

private:
    clang::Sema& sema;
};

/// This class is used to resolve dependent names in the AST.
/// For dependent names, we cannot know the any information about the name until
/// the template is instantiated. This can be frustrating, you cannot get
/// completion, you cannot get go-to-definition, etc. To avoid this, we just use
/// some heuristics to simplify the dependent names as normal type/expression.
/// For example, `std::vector<T>::value_type` can be simplified as `T`.
class DependentNameResolver {

public:
    DependentNameResolver(clang::Sema& sema, clang::ASTContext& context) : sema(sema), context(context) {}

    clang::QualType resolve(clang::NamedDecl* ND);

    clang::QualType resolve(clang::QualType type);

    clang::QualType resolve(const clang::DependentNameType* DNT);

    clang::QualType resolve(const clang::DependentTemplateSpecializationType* DTST);

    /// lookup member in a given nested name specifier
    bool lookup(llvm::SmallVector<clang::NamedDecl*>& result,
                const clang::NestedNameSpecifier* NNS,
                const clang::IdentifierInfo* II);

    bool lookup(llvm::SmallVector<clang::NamedDecl*>& result,
                const clang::QualType type,
                const clang::IdentifierInfo* II);

    // lookup member in a given class template
    // FIXME: search in base classes
    bool lookup(llvm::SmallVector<clang::NamedDecl*>& result,
                clang::ClassTemplateDecl* CTD,
                const clang::IdentifierInfo* II,
                llvm::ArrayRef<clang::TemplateArgument> arguments);

    std::vector<clang::TemplateArgument> resugar(llvm::ArrayRef<clang::TemplateArgument> arguments);

    /// we use `Sema::SubstType` to substitute the template arguments in dependent type.
    /// but it doesn't substitute the template arguments in alias type.
    /// i.e. `typename base::type`, when base is `std::vector<T>`, it will ignore the `T`.
    /// so before actually substituting the type, we need to dealias the type.
    clang::QualType dealias(clang::QualType type);

    /// replace the template arguments in the type, using the arguments in the frame
    clang::QualType substitute(clang::QualType type);

private:
    struct Frame {
        clang::NamedDecl* decl;
        std::vector<clang::TemplateArgument> arguments;
    };

    clang::Sema& sema;
    clang::ASTContext& context;
    std::vector<Frame> frames;
};

}  // namespace clice
