#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "alisp.h"

TEST_CASE("Encode positive integer", "[objects]")
{
    REQUIRE(0x0 == Objects::encodeInteger(0));
    REQUIRE(0b0000'0100 == Objects::encodeInteger(1));
    REQUIRE(0b0010'1000 == Objects::encodeInteger(10));
}

TEST_CASE("Encode negative integer", "[objects]")
{
    REQUIRE(0x0 == Objects::encodeInteger(0));
    REQUIRE((word)0xfffffffffffffffc == Objects::encodeInteger(-1));
    REQUIRE((word)0xffffffffffffffd8 == Objects::encodeInteger(-10));
}

TEST_CASE("Encode char", "[objects]")
{
    REQUIRE(0b0000'1111 == Objects::encodeChar(0));
    REQUIRE(0b0110'0001'0000'1111 == Objects::encodeChar('a'));
    REQUIRE(0b0111'1010'0000'1111 == Objects::encodeChar('z'));
}

TEST_CASE("Encode bool", "[objects]")
{
    REQUIRE(0b1001'1111 == Objects::encodeBool(true));
    REQUIRE(0b0001'1111 == Objects::encodeBool(false));
}

TEST_CASE("Env::find", "[env]"){
    Env e1 { "alpha", 1, nullptr};
    Env e2 { "beta", 2, &e1};
    SECTION("can find alpha") {
        REQUIRE(*e2.find("alpha") == 1);
    }
    SECTION("can find beta") {
        REQUIRE(*e2.find("beta") == 2);
    }
    SECTION("can't find an unknown value") {
        REQUIRE(!e2.find("gamma"));
    }
}

TEST_CASE("Compile positive integer", "[compiler]")
{
    word value = 123;
    auto node = ASTNode::newInteger(value);

    Buffer buf;
    auto compileResult = Compile::function(buf, node);
    REQUIRE(compileResult == 0);

    std::vector<uint8_t> expected = {
        0x55,                                     // push rbp
        0x48, 0x89, 0xe5,                         // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, // mov eax, 123
        0x5d,                                     // pop rbp
        0xc3                                      // ret
    };
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeInteger(value));
}

TEST_CASE("Compile negative integer", "[compiler]")
{
    word value = -123;
    auto node = ASTNode::newInteger(value);

    Buffer buf;
    auto compileResult = Compile::function(buf, node);
    REQUIRE(compileResult == 0);

    std::vector<uint8_t> expected = {
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x14, 0xfe, 0xff, 0xff, // mov rax, -123
        0x5d,
        0xc3 // ret
    };
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeInteger(value));
}

TEST_CASE("Compile char", "[compiler]")
{
    char value = 'a';
    auto node = ASTNode::newChar(value);
    Buffer buf;
    auto compileResult = Compile::function(buf, node);
    REQUIRE(compileResult == 0);

    std::vector<uint8_t> expected{
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x0f, 0x61, 0x00, 0x00,
        0x5d,
        0xc3};
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();

    REQUIRE(code.toFunc<int()>()() == Objects::encodeChar(value));
}

TEST_CASE("Compile true", "[compiler]")
{
    auto value = true;
    auto node = ASTNode::newBool(value);
    Buffer buf;
    auto compileResult = Compile::function(buf, node);
    REQUIRE(compileResult == 0);

    std::vector<uint8_t> expected{
        0x55,             // push rbp
        0x48, 0x89, 0xe5, // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0x9f, 0x0, 0x0, 0x0,
        0x5d,
        0xc3};
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();

    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(value));
}

TEST_CASE("Compile false", "[compiler]")
{
    auto value = false;
    auto node = ASTNode::newBool(value);
    Buffer buf;
    auto compileResult = Compile::function(buf, node);
    REQUIRE(compileResult == 0);

    std::vector<uint8_t> expected{
        0x55,             // push rbp
        0x48, 0x89, 0xe5, // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0x1f, 0x00, 0x00, 0x00,
        0x5d,
        0xc3};
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();

    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(value));
}

TEST_CASE("Compile nil", "[compiler]")
{
    Buffer buf;
    auto compileResult = Compile::function(buf, ASTNode::nil());
    REQUIRE(compileResult == 0);
    std::vector<uint8_t> expected = {
        0x55,             // push rbp
        0x48, 0x89, 0xe5, // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0x2f, 0x00, 0x00, 0x00,
        0x5d,
        0xc3};

    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();

    REQUIRE(code.toFunc<int()>()() == Objects::nil());
}

static auto wrap(ASTNode* node){
    return std::unique_ptr<ASTNode, decltype(&heapFree)>(node, heapFree);
}

static std::unique_ptr<ASTNode, decltype(&heapFree)> makeUnaryCall(const std::string_view &name, ASTNode *arg)
{
    return wrap(ASTNode::newUnaryCall(name, arg));
}

TEST_CASE("Compile unary add1", "[compiler]")
{
    Buffer buf;
    auto node = makeUnaryCall("add1", ASTNode::newInteger(123));

    REQUIRE(0 == Compile::function(buf, node.get()));

    std::vector<uint8_t> expected{
        0x55,                                     // push rbp
        0x48, 0x89, 0xe5,                         // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, // mov rax, imm(123)
        0x48, 0x05, 0x04, 0x00, 0x00, 0x00,       // add rax, imm(1)
        0x5d,                                     // pop rbp
        0xc3                                      // ret
    };
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeInteger(124));
}

TEST_CASE("Compile unary add1 nested", "[compiler]")
{
    Buffer buf;
    auto node = makeUnaryCall("add1", ASTNode::newUnaryCall("add1", ASTNode::newInteger(123)));

    REQUIRE(0 == Compile::function(buf, node.get()));
    std::vector<uint8_t> expected{
        0x55,                                     // push rbp
        0x48, 0x89, 0xe5,                         // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, // mov rax, imm(123)
        0x48, 0x05, 0x04, 0x00, 0x00, 0x00,       // add rax, imm(1)
        0x48, 0x05, 0x04, 0x00, 0x00, 0x00,       // add rax, imm(1)
        0x5d,                                     // pop rbp
        0xc3};                                    // ret
    REQUIRE(expected == buf._buf);

    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeInteger(125));
}

TEST_CASE("compile boolean? with non-boolean returns false", "[compiler]")
{
    Buffer buf;
    auto node = makeUnaryCall("boolean?", ASTNode::newInteger(5));
    REQUIRE(0 == Compile::function(buf, node.get()));
    std::vector<uint8_t> expected{
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, // mov    rax,0x14
        0x48, 0x83, 0xe0, 0x1f,                   // and    rax,0x3f // a typo?
        0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00,       // cmp    rax,0x0000001f
        0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, // mov    rax,0x0
        0x0f, 0x94, 0xc0,                         // sete   al
        0x48, 0xc1, 0xe0, 0x07,                   // shl    rax,0x7
        0x48, 0x83, 0xc8, 0x1f,                   // or     rax,0x1f
        0x5d,
        0xc3};
    REQUIRE(expected == buf._buf);
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(false));
}

TEST_CASE("compile boolean? with true returns true", "[compiler]")
{
    Buffer buf;
    auto node = makeUnaryCall("boolean?", ASTNode::newBool(true));
    REQUIRE(0 == Compile::function(buf, node.get()));
    std::vector<uint8_t> expected{
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x9f, 0x00, 0x00, 0x00, // mov    rax,0x9f
        0x48, 0x83, 0xe0, 0x1f,                   // and    rax,0x1f
        0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00,       // cmp    rax,0x0000001f
        0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, // mov    rax,0x0
        0x0f, 0x94, 0xc0,                         // sete   al
        0x48, 0xc1, 0xe0, 0x07,                   // shl    rax,0x7
        0x48, 0x83, 0xc8, 0x1f,                   //  or     rax,0x1f
        0x5d,
        0xc3}; // ret
    REQUIRE(expected == buf._buf);
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(true));
}

TEST_CASE("compile boolean? with false returns true", "[compiler]")
{
    Buffer buf;
    auto node = makeUnaryCall("boolean?", ASTNode::newBool(false));
    REQUIRE(0 == Compile::function(buf, node.get()));
    std::vector<uint8_t> expected{
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x1f, 0x00, 0x00, 0x00, // mov    rax,0x1f
        0x48, 0x83, 0xe0, 0x1f,                   // and    rax,0x1f
        0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00,       // cmp    rax,0x0000001f
        0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, // mov    rax,0x0
        0x0f, 0x94, 0xc0,                         // sete   al
        0x48, 0xc1, 0xe0, 0x07,                   // shl    rax,0x7
        0x48, 0x83, 0xc8, 0x1f,                   // or     rax,0x1f
        0x5d,
        0xc3}; // ret
    REQUIRE(expected == buf._buf);
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(true));
}

static auto makeBinaryCall(const std::string_view &name, ASTNode *arg1, ASTNode *arg2)
{
    return wrap(ASTNode::newBinaryCall(name, arg1, arg2));
}

TEST_CASE("Compile binary +", "[compiler]")
{
    Buffer buf;
    auto node = makeBinaryCall("+", ASTNode::newInteger(5), ASTNode::newInteger(8));
    REQUIRE(0 == Compile::function(buf, node.get()));
    std::vector<uint8_t> expected{
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x20, 0x00, 0x00, 0x00, // mov    rax,0x20
        0x48, 0x89, 0x45, 0xf8,                   // mov    QWORD PTR [rbp-0x8],rax
        0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, // mov    rax,0x14
        0x48, 0x03, 0x45, 0xf8,                   // add    rax,QWORD PTR [rbp-0x8]
        0x5d,
        0xc3};
    REQUIRE(expected == buf._buf);
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeInteger(13));
}

TEST_CASE("Compile binary -", "[compiler]")
{
    Buffer buf;
    auto node = makeBinaryCall("-", ASTNode::newInteger(5), ASTNode::newInteger(8));
    REQUIRE(0 == Compile::function(buf, node.get()));
    std::vector<uint8_t> expected{
        0x55,
        0x48, 0x89, 0xe5,
        0x48, 0xc7, 0xc0, 0x20, 0x00, 0x00, 0x00, // mov    rax,0x20
        0x48, 0x89, 0x45, 0xf8,                   // mov    QWORD PTR [rbp-0x8],rax
        0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, // mov    rax,0x14
        0x48, 0x2b, 0x45, 0xf8,                   // sub    rax,QWORD PTR [rbp-0x8]
        0x5d,
        0xc3};
    REQUIRE(expected == buf._buf);
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeInteger(5 - 8));
}

TEST_CASE("Compile binary = with LHS equal to RHS returns true", "[compiler]")
{
    Buffer buf;
    auto node = makeBinaryCall("=", ASTNode::newInteger(5), ASTNode::newInteger(5));
    REQUIRE(0 == Compile::function(buf, node.get()));
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(true));
}

TEST_CASE("Compile binary = with LHS not equal to RHS returns false", "[compiler]")
{
    Buffer buf;
    auto node = makeBinaryCall("=", ASTNode::newInteger(6), ASTNode::newInteger(5));
    REQUIRE(0 == Compile::function(buf, node.get()));
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(false));
}

TEST_CASE("Compile binary < with LHS less than RHS returns true", "[compiler]")
{
    Buffer buf;
    auto node = makeBinaryCall("<", ASTNode::newInteger(5), ASTNode::newInteger(6));
    REQUIRE(0 == Compile::function(buf, node.get()));
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(true));
}

TEST_CASE("Compile binary < with LHS greater than RHS returns false", "[compiler]")
{
    Buffer buf;
    auto node = makeBinaryCall("<", ASTNode::newInteger(6), ASTNode::newInteger(5));
    REQUIRE(0 == Compile::function(buf, node.get()));
    auto code = buf.freeze();
    REQUIRE(code.toFunc<int()>()() == Objects::encodeBool(false));
}

TEST_CASE("Read with unsigned integer returns integer", "[reader]")
{
    auto node = Reader::read("1234");
    REQUIRE(1234 == node->getInteger());
}

TEST_CASE("Read with positive integer returns integer", "[reader]")
{
    auto node = Reader::read("+1234");
    REQUIRE(1234 == node->getInteger());
}

TEST_CASE("Read with negative integer returns integer", "[reader]")
{
    auto node = Reader::read("-1234");
    REQUIRE(-1234 == node->getInteger());
}

TEST_CASE("Leading whitespaces are ignored", "[reader]")
{
    auto node = Reader::read("   \t   \n  1234");
    REQUIRE(1234 == node->getInteger());
}

TEST_CASE("Read with list returns list", "[reader]"){
    auto node = Reader::read("(1 2 0)");
    REQUIRE(node->isPair());
    auto pair = node->asPair();
    REQUIRE(pair->car->getInteger() == 1);
    pair = pair->cdr->asPair();
    REQUIRE(pair->car->getInteger() == 2);
    pair = pair->cdr->asPair();
    REQUIRE(pair->car->getInteger() == 0);
    REQUIRE(pair->cdr->isNil());
}

TEST_CASE("Read with list with spaces returns list", "[reader]"){
    auto node = Reader::read("( 1\t2 0  )");
    REQUIRE(node->isPair());
    auto pair = node->asPair();
    REQUIRE(pair->car->getInteger() == 1);
    pair = pair->cdr->asPair();
    REQUIRE(pair->car->getInteger() == 2);
    pair = pair->cdr->asPair();
    REQUIRE(pair->car->getInteger() == 0);
    REQUIRE(pair->cdr->isNil());
}

TEST_CASE("Read with symbol returns symbol", "[reader]")
{
    auto node = Reader::read("hello?+-*=>");
    REQUIRE("hello?+-*=>" == node->asSymbol()->str);
}

TEST_CASE("Read with symbol with trailing spaces", "[reader]"){
    auto node = Reader::read("add1 1");
    REQUIRE(node->isSymbol());
    REQUIRE(node->asSymbol()->str == "add1");
}
