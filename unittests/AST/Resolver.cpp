#include "../Test.h"
#include <Compiler/Compiler.h>
#include <Compiler/Resolver.h>

namespace {

using namespace clice;

struct TemplateResolverTester : public clang::RecursiveASTVisitor<TemplateResolverTester> {
    TemplateResolverTester(llvm::StringRef code) {
        compileArgs = {
            "clang++",
            "-std=c++20",
            "main.cpp",
            "-resource-dir",
            "/home/ykiko/C++/clice2/build/lib/clang/20",
        };
        compiler = std::make_unique<Compiler>("main.cpp", code, compileArgs);
        compiler->buildAST();
        test();
    }

    bool VisitTypeAliasDecl(clang::TypeAliasDecl* decl) {
        if(decl->getName() == "input") {
            input = decl->getUnderlyingType();
        }

        if(decl->getName() == "expect") {
            expect = decl->getUnderlyingType();
        }

        return true;
    }

    void test(std::source_location location = std::source_location::current()) {
        TraverseDecl(compiler->tu());
        EXPECT_FALSE(input.isNull());
        EXPECT_FALSE(expect.isNull());

        auto& resolver = compiler->resolver();
        clang::QualType result = resolver.resolve(input);
        EXPECT_EQ(result.getCanonicalType(), expect.getCanonicalType());

        /// Test whether cache works.
        clang::QualType result2 = resolver.resolve(input);
        EXPECT_EQ(result, result2);
    }

    clang::QualType input;
    clang::QualType expect;
    std::vector<const char*> compileArgs;
    std::unique_ptr<clice::Compiler> compiler;
};

TEST(TemplateResolver, TypeParameterType) {
    TemplateResolverTester tester(R"cpp(
template <typename T>
struct A {
    using type = T;
};

template <typename X>
struct test {
    using input = typename A<X>::type;
    using expect = X;
};
)cpp");
}

TEST(TemplateResolver, SingleLevel) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T>
struct A {
    using type = type_list<T>;
};

template <typename X>
struct test {
    using input = typename A<X>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, SingleLevelNotDependent) {
    TemplateResolverTester tester(R"cpp(
template <typename T>
struct A {
    using type = int;
};

template <typename X>
struct test {
    using input = typename A<X>::type;
    using expect = int;
};
)cpp");
}

TEST(TemplateResolver, MultiLevel) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    using type = type_list<T1>;
};

template <typename T2>
struct B {
    using type = typename A<T2>::type;
};

template <typename T3>
struct C {
    using type = typename B<T3>::type;
};

template <typename X>
struct test {
    using input = typename C<X>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, MultiLevelNotDependent) {
    TemplateResolverTester tester(R"cpp(
template <typename T1>
struct A {
    using type = int;
};

template <typename T2>
struct B {
    using type = typename A<T2>::type;
};

template <typename T3>
struct C {
    using type = typename B<T3>::type;
};

template <typename X>
struct test {
    using input = typename C<X>::type;
    using expect = int;
};
)cpp");
}

TEST(TemplateResolver, ArgumentDependent) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    using type = T1;
};

template <typename T2>
struct B {
    using type = type_list<T2>;
};

template <typename X>
struct test {
    using input = typename B<typename A<X>::type>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, AliasArgument) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    using type = T1;
};

template <typename T2>
struct B {
    using base = A<T2>;
    using type = type_list<typename base::type>;
};

template <typename X>
struct test {
    using input = typename B<X>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, AliasDependent) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    using type = type_list<T1>;
};

template <typename T2>
struct B {
    using base = A<T2>;
    using type = typename base::type;
};

template <typename X>
struct test {
    using input = typename B<X>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, AliasTemplate) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1, typename U1>
struct A {
    using type = type_list<T1, U1>;
};

template <typename T2>
struct B {
    template <typename U2>
    using type = typename A<T2, U2>::type;
};

template <typename X, typename Y>
struct test {
    using input = typename B<X>::template type<Y>;
    using expect = type_list<X, Y>;
};
)cpp");
}

TEST(TemplateResolver, BaseDependent) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    using type = type_list<T1>;
};

template <typename U2>
struct B : A<U2> {};

template <typename X>
struct test {
    using input = typename B<X>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, MultiNested) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    using self = A<T1>;
    using type = type_list<T1>;
};

template <typename X>
struct test {
    using input = typename A<X>::self::self::self::self::self::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, DependentMemberClass) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {
    template <typename T2>
    struct B {
        template <typename T3>
        struct C {
            using type = type_list<T1, T2, T3>;
        };
    };
};

template <typename X, typename Y, typename Z>
struct test {
    using input = typename A<X>::template B<Y>::template C<Z>::type;
    using expect = type_list<X, Y, Z>;
};
)cpp");
}

TEST(TemplateResolver, PartialSpecialization) {
    TemplateResolverTester tester(R"cpp(
template <typename... Ts>
struct type_list {};

template <typename T1>
struct A {};

template <typename U2>
struct B {};

template <typename U2, template <typename...> typename HKT>
struct B<HKT<U2>> {
    using type = type_list<U2>;
};

template <typename X>
struct test {
    using input = typename B<A<X>>::type;
    using expect = type_list<X>;
};
)cpp");
}

TEST(TemplateResolver, Standard) {
    TemplateResolverTester tester(R"cpp(
#include <vector>

template <typename T>
struct test {
    using input = typename std::vector<T>::reference;
    using expect = T&;
};
)cpp");
}

}  // namespace

