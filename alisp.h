#pragma once

#include <vector>
#include <memory>
#include <string>
#include <optional>

using JitFunction = int (*)();

struct Code final
{
    Code(const std::vector<uint8_t> &buf);

    static void VFree(uint8_t *ptr);

    template <typename TF>
    auto toFunc() const
    {
        return reinterpret_cast<TF *>(_ptr.get());
    }

private:
    std::unique_ptr<uint8_t, void (*)(uint8_t *)> _ptr;
};

struct Buffer final
{
    void write8(uint8_t v);
    void write32(uint32_t v);
    void writeArray(const uint8_t array[], size_t size);

    Code freeze() const;

    std::vector<uint8_t> _buf;
};

// Objects
using word = int64_t;
using uword = uint64_t;

constexpr int BitsPerByte = 8;
constexpr int WordSize = sizeof(word);
constexpr int BitsPerWord = WordSize * BitsPerByte;

namespace Objects
{
    constexpr unsigned int ImmediateTagMask = 0x3f;

    constexpr unsigned int IntegerTag = 0x0;
    constexpr unsigned int IntegerMask = 0b0000'0011;
    constexpr unsigned int IntegerShift = 2;
    constexpr unsigned int IntegerBits = BitsPerWord - IntegerShift;
    constexpr word IntegerMax = (1LL << (IntegerBits - 1)) - 1;
    constexpr word IntegerMin = -(1LL << (IntegerBits - 1));

    constexpr unsigned int CharTag = 0b0000'1111;
    constexpr unsigned int CharMask = 0b1111'1111;
    constexpr unsigned int CharShift = 8;

    constexpr unsigned int BoolTag = 0b0001'1111;
    constexpr unsigned int BoolMask = 0b1000'0000;
    constexpr unsigned int BoolShift = 7;

    constexpr unsigned int PairTag = 0b0000'0001;
    constexpr uword HeapTagMask = 0b0000'0111;
    constexpr uword HeapPtrMask = ~HeapTagMask;

    constexpr unsigned int SymbolTag = 0b0000'0101;

    constexpr unsigned int ErrorTag = 0b0011'1111;

    word encodeInteger(word value);
    word decodeInteger(word value);

    word encodeChar(char value);
    char decodeChar(word value);

    word encodeBool(bool value);
    bool decodeBool(word value);

    word nil();

    uword address(const void *obj);

    uword error();
} // namespace Objects

// AST
struct Pair;
struct Symbol;

struct ASTNode final
{
    static ASTNode *newInteger(word value);
    bool isInteger() const;
    word getInteger() const;

    static ASTNode *newChar(char value);
    bool isChar() const;
    char getChar() const;

    static ASTNode *newBool(bool value);
    bool isBool() const;
    bool getBool() const;

    static ASTNode *nil();
    bool isNil() const;

    static ASTNode *newPair(ASTNode *car, ASTNode *cdr);
    bool isPair() const;
    Pair *asPair() const;

    static ASTNode *newSymbol(const std::string_view &name);
    bool isSymbol() const;
    Symbol *asSymbol() const;

    static ASTNode *newUnaryCall(const std::string_view &name, ASTNode *arg);
    static ASTNode *newBinaryCall(const std::string_view &name, ASTNode *arg1, ASTNode* arg2);

    static ASTNode* error();
    bool isError() const;
};
static_assert(sizeof(ASTNode *) == sizeof(word), "Must be able to cast ASTNode* to word and back");

ASTNode *heapAlloc(uint8_t tag, uword size);
void heapFree(ASTNode *node);

struct Pair
{
    ASTNode *car{};
    ASTNode *cdr{};
};

struct Symbol
{
    std::string str{};
};

struct Env{
    std::string name;
    word value;
    Env* prev;

    std::optional<word> find(const std::string_view& name) const;
};

// Emit
namespace Emit
{
    enum Register : uint8_t
    {
        Rax = 0,
        Rcx,
        Rdx,
        Rbx,
        Rsp,
        Rbp,
        Rsi,
        Rdi,
    };

    enum PartialRegister : uint8_t
    {
        Al = 0,
        Cl,
        Dl,
        Bl,
        Ah,
        Ch,
        Dh,
        Bh
    };
    enum Condition : uint8_t
    {
        Overflow = 0,
        NotOverflow = 1,
        Carry = 2,    // Below
        NotCarry = 3, // AboveEqual, NotBelow
        Equal = 4,    // Zero
        NotEqual = 5, // NotZero

        Sign = 8,
        Less = 0xc,
        // Etc. See https://c9x.me/x86/html/file_module_x86_id_288.html
    };

    struct Indirect{
        Register reg;
        int8_t disp;
    };

    void movRegImm32(Buffer &buf, Register dst, int32_t src);
    void addRegImm32(Buffer &buf, Register dst, int32_t src);
    void shlRegImm8(Buffer &buf, Register dst, uint8_t src);
    void shrRegImm8(Buffer &buf, Register dst, uint8_t src);
    void orRegImm8(Buffer &buf, Register dst, uint8_t src);
    void andRegImm8(Buffer &buf, Register dst, uint8_t src);
    void cmpRegImm32(Buffer &buf, Register left, int32_t right);
    void setccImm8(Buffer &buf, Condition cond, PartialRegister dst);
    void ret(Buffer &buf);
    void storeIndirectReg(Buffer& buf, const Indirect& dst, const Register src);
    void addRegIndirect(Buffer& buf, Register dst, const Indirect& src);
    void cmpRegIndirect(Buffer& buf, Register left, const Indirect& right);
} // namespace Emit

namespace Compile
{
    int expr(Buffer &buf, ASTNode *node, word stackIndex);
    int function(Buffer &buf, ASTNode *node);
} // namespace Compile

namespace Reader{
    std::unique_ptr<ASTNode, decltype(&heapFree)> read(const std::string& input);
}