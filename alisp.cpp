#include "alisp.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <string>
#include <cassert>
#include <cctype>

Code::Code(const std::vector<uint8_t> &buf)
    : _ptr{reinterpret_cast<unsigned char *>(::VirtualAlloc(nullptr, std::size(buf), MEM_COMMIT, PAGE_READWRITE)),
           &VFree}
{
    std::copy(buf.cbegin(), buf.cend(), _ptr.get());
    DWORD oldProtect;
    auto protResult = VirtualProtect(_ptr.get(), buf.size(), PAGE_EXECUTE, &oldProtect);
    assert(protResult);
}

void Code::VFree(uint8_t *ptr)
{
    VirtualFree(ptr, 0, MEM_RELEASE);
}

void Buffer::write8(uint8_t v) { _buf.push_back(v); }

void Buffer::write32(uint32_t v)
{
    for (uint8_t i = 0; i < 4; ++i)
    {
        write8(static_cast<uint8_t>(v >> i * BitsPerByte));
    }
}

void Buffer::writeArray(const uint8_t array[], size_t size)
{
    for (auto i = 0u; i < size; ++i)
    {
        write8(array[i]);
    }
}

void Buffer::writeAt32(size_t pos, uint32_t v)
{
    for (auto i = 0u; i < 4; ++i)
    {
        _buf.at(pos + i) = static_cast<uint8_t>(v >> i * BitsPerByte);
    }
}

size_t Buffer::size() const
{
    return _buf.size();
}

Code Buffer::freeze() const
{
    return Code{_buf};
}

namespace Objects
{
    word encodeInteger(word value)
    {
        assert(value < IntegerMax && "too big");
        assert(value > IntegerMin && "too small");
        return value << IntegerShift;
    }
    word decodeInteger(word value)
    {
        return value >> IntegerShift;
    }

    word encodeChar(char value)
    {
        return ((word)value << CharShift) | CharTag;
    }
    char decodeChar(word value)
    {
        return (value >> CharShift) & CharMask;
    }

    word encodeBool(bool value)
    {
        return ((word)value << BoolShift) | BoolTag;
    }
    bool decodeBool(word value)
    {
        return value & BoolMask;
    }

    word nil() { return 0b0010'1111; }

    uword address(const void *obj)
    {
        return reinterpret_cast<uword>(obj) & HeapPtrMask;
    }

    uword error() { return ErrorTag; }
} // namespace Objects

ASTNode *ASTNode::newInteger(word value)
{
    return reinterpret_cast<ASTNode *>(Objects::encodeInteger(value));
}

bool ASTNode::isInteger() const
{
    return (reinterpret_cast<word>(this) & Objects::IntegerMask) == Objects::IntegerTag;
}

word ASTNode::getInteger() const
{
    return Objects::decodeInteger(reinterpret_cast<word>(this));
}

ASTNode *ASTNode::newChar(char value)
{
    return reinterpret_cast<ASTNode *>(Objects::encodeChar(value));
}

bool ASTNode::isChar() const
{
    return (reinterpret_cast<word>(this) & Objects::ImmediateTagMask) == Objects::CharTag;
}

char ASTNode::getChar() const
{
    return Objects::decodeChar(reinterpret_cast<word>(this));
}

ASTNode *ASTNode::newBool(bool value)
{
    return reinterpret_cast<ASTNode *>(Objects::encodeBool(value));
}
bool ASTNode::isBool() const
{
    return (reinterpret_cast<word>(this) & Objects::ImmediateTagMask) == Objects::BoolTag;
}
bool ASTNode::getBool() const
{
    return Objects::decodeBool(reinterpret_cast<word>(this));
}

ASTNode *ASTNode::nil()
{
    return reinterpret_cast<ASTNode *>(Objects::nil());
}

bool ASTNode::isNil() const
{
    return reinterpret_cast<word>(this) == Objects::nil();
}

ASTNode *heapAlloc(uint8_t tag, uword size)
{
    auto address = reinterpret_cast<uintptr_t>(new uint8_t[size]);
    return reinterpret_cast<ASTNode *>(address | tag);
}

static bool isHeapObject(ASTNode *node)
{
    return (reinterpret_cast<uintptr_t>(node) & Objects::HeapPtrMask) == Objects::HeapTagMask;
}

void heapFree(ASTNode *node)
{
    if (!isHeapObject(node))
    {
        return;
    }
    if (node->isPair())
    {
        auto pair = node->asPair();
        heapFree(pair->car);
        pair->car = nullptr;
        heapFree(pair->cdr);
        pair->cdr = nullptr;
    }
    delete[](reinterpret_cast<uint8_t *>(Objects::address(node)));
}

ASTNode *ASTNode::newPair(ASTNode *car, ASTNode *cdr)
{
    auto node = heapAlloc(Objects::PairTag, sizeof(Pair));
    auto pair = node->asPair();
    new (pair) Pair{};
    pair->car = car;
    pair->cdr = cdr;

    return node;
}

bool ASTNode::isPair() const
{
    return (reinterpret_cast<uword>(this) & Objects::HeapTagMask) == Objects::PairTag;
}

Pair *ASTNode::asPair() const
{
    assert(isPair());
    return (Pair *)Objects::address(reinterpret_cast<const void *>(this));
}

ASTNode *ASTNode::newSymbol(const std::string_view &name)
{
    auto node = heapAlloc(Objects::SymbolTag, sizeof(Symbol));
    auto symbol = node->asSymbol();
    new (symbol) Symbol{};
    symbol->str = name;
    return node;
}
bool ASTNode::isSymbol() const
{
    return (reinterpret_cast<uintptr_t>(this) & Objects::HeapTagMask) == Objects::SymbolTag;
}
Symbol *ASTNode::asSymbol() const
{
    assert(isSymbol());
    return (Symbol *)Objects::address(reinterpret_cast<const void *>(this));
}

ASTNode *ASTNode::newUnaryCall(const std::string_view &name, ASTNode *arg)
{
    return newPair(newSymbol(name), newPair(arg, nil()));
}

ASTNode *ASTNode::newBinaryCall(const std::string_view &name, ASTNode *arg1, ASTNode *arg2)
{
    return newPair(newSymbol(name), newPair(arg1, newPair(arg2, nil())));
}

ASTNode *ASTNode::error() { return reinterpret_cast<ASTNode *>(Objects::error()); }
bool ASTNode::isError() const
{
    return reinterpret_cast<uword>(this) == Objects::error();
}

std::optional<word> Env::find(const std::string_view &name) const
{
    for (auto env = this; env; env = env->prev)
    {
        if (name == env->name)
        {
            return env->value;
        }
    }
    return {};
}

namespace Emit
{
    constexpr uint8_t RexPrefix = 0x48;

    void movRegReg(Buffer &buf, Register dst, Register src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x89);
        buf.write8(0xc0 | (src << 3) | dst);
    }
    void movRegImm32(Buffer &buf, Register dst, int32_t src)
    {
        buf.write8(RexPrefix);
        buf.write8(0xc7);
        buf.write8(0xc0 | dst);
        buf.write32(src);
    }
    void addRegImm32(Buffer &buf, Register dst, int32_t src)
    {
        buf.write8(RexPrefix);
        if (dst == Emit::Rax)
        {
            buf.write8(5);
        }
        else
        {
            buf.write8(0x81);
            buf.write8(0xc0 | dst);
        }
        buf.write32(src);
    }
    void shlRegImm8(Buffer &buf, Register dst, uint8_t src)
    {
        buf.write8(RexPrefix);
        buf.write8(0xc1);
        buf.write8(0xe0 | dst);
        buf.write8(src);
    }
    void shrRegImm8(Buffer &buf, Register dst, uint8_t src)
    {
        buf.write8(RexPrefix);
        buf.write8(0xc1); // todo: look up the opcode
        buf.write8(0xe8 | dst);
        buf.write8(src);
    }
    void orRegImm8(Buffer &buf, Register dst, uint8_t src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x83); // todo: look up the opcode
        buf.write8(0xc8 | dst);
        buf.write8(src);
    }
    void andRegImm8(Buffer &buf, Register dst, uint8_t src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x83); // todo: look up the opcode
        buf.write8(0xe0 | dst);
        buf.write8(src);
    }
    void cmpRegImm32(Buffer &buf, Register left, int32_t right)
    {
        buf.write8(RexPrefix);
        if (left == Rax)
        {
            buf.write8(0x3d);
        }
        else
        {
            buf.write8(0x81);
            buf.write8(0xf8 | left);
        }
        buf.write32(right);
    }
    void setccImm8(Buffer &buf, Condition cond, PartialRegister dst)
    {
        buf.write8(0x0f);
        buf.write8(0x90 | cond);
        buf.write8(0xc0 | dst);
    }
    void ret(Buffer &buf)
    {
        buf.write8(0xc3);
    }

    static uint8_t disp8(int8_t disp) { return disp >= 0 ? disp : 0x100 + disp; }
    static uint32_t disp32(int32_t disp) { return disp >= 0 ? disp : static_cast<uint32_t>(0x1'0000'0000 + disp); }

    void storeIndirectReg(Buffer &buf, const Indirect &dst, const Register src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x89);
        buf.write8(0x40 | (src << 3) | dst.reg);
        buf.write8(disp8(dst.disp));
    }
    void loadRegIndirect(Buffer &buf, Register dst, const Indirect &src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x8b);
        buf.write8(0x40 | (dst << 3) | src.reg);
        buf.write8(disp8(src.disp));
    }
    void addRegIndirect(Buffer &buf, Register dst, const Indirect &src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x3);
        buf.write8(0x40 | (dst << 3) | src.reg);
        buf.write8(disp8(src.disp));
    }
    void subRegIndirect(Buffer &buf, Register dst, const Indirect &src)
    {
        buf.write8(RexPrefix);
        buf.write8(0x2b);
        buf.write8(0x40 | (dst << 3) | src.reg);
        buf.write8(disp8(src.disp));
    }
    void cmpRegIndirect(Buffer &buf, Register left, const Indirect &right)
    {
        buf.write8(RexPrefix);
        buf.write8(0x3b);
        buf.write8(0x40 | (left << 3) | right.reg);
        buf.write8(disp8(right.disp));
    }
    word jcc(Buffer &buf, Condition cond, int32_t offset)
    {
        buf.write8(0x0f);
        buf.write8(0x80 | cond);
        auto pos = buf.size();
        buf.write32(disp32(offset));
        return static_cast<word>(pos);
    }
    word jmp(Buffer &buf, int32_t offset)
    {
        buf.write8(0xe9);
        auto pos = buf.size();
        buf.write32(disp32(offset));
        return static_cast<word>(pos);
    }

    void backpatchImm32(Buffer &buf, size_t targetPos)
    {
        auto currentPos = buf.size();
        auto relativePos = static_cast<int32_t>(currentPos - targetPos - sizeof(int32_t));
        buf.writeAt32(targetPos, disp32(relativePos));
    }
} // namespace Emit

namespace Compile
{
    static const uint8_t FunctionPrologue[] = {
        // Win64 ABI passes first arg in ecx, not edi like UNIXes do
        Emit::RexPrefix, 0x89, 0xce, //  mov esi, ecx
        0x55,                        // push rbp
        Emit::RexPrefix, 0x89, 0xe5, // mov rbp, rsp
    };
    static const uint8_t FunctionEpilogue[] = {
        0x5d, // pop rbp
        0xc3  // ret
    };

#define _(EXPR)                   \
    do                            \
    {                             \
        if (auto result = (EXPR)) \
            return result;        \
    } while (0);

    void compareInt32(Buffer &buf, int32_t value)
    {
        using namespace Emit;
        cmpRegImm32(buf, Rax, value);
        movRegImm32(buf, Rax, 0);
        setccImm8(buf, Equal, Al);
        shlRegImm8(buf, Rax, Objects::BoolShift);
        orRegImm8(buf, Rax, Objects::BoolTag);
    }

    ASTNode *operand1(ASTNode *list)
    {
        return list->asPair()->car;
    }
    ASTNode *operand2(ASTNode *list)
    {
        return list->asPair()->cdr->asPair()->car;
    }
    ASTNode *operand3(ASTNode *list)
    {
        return list->asPair()->cdr->asPair()->cdr->asPair()->car;
    }

    int let(Buffer &buf, ASTNode *bindings, ASTNode *body, word stackIndex, const Env *bindingEnv, const Env *bodyEnv)
    {
        if (bindings->isNil())
        {
            // Base case: no bindings. Compile the body
            _(expr(buf, body, stackIndex, bodyEnv));
            return 0;
        }
        else
        {
            assert(bindings->isPair());
            // Get the next binding
            auto pair = bindings->asPair();
            assert(pair->car->isPair());
            auto binding = pair->car->asPair();
            auto name = binding->car;
            assert(name->isSymbol());
            auto bindingExpr = binding->cdr->asPair()->car;
            // Compile the binding expression
            _(expr(buf, bindingExpr, stackIndex, bindingEnv));
            Emit::storeIndirectReg(buf, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)}, Emit::Rax);
            // Bind the name
            Env entry{name->asSymbol()->str, stackIndex, bodyEnv};
            // process the rest of bindings recursively
            _(let(buf, pair->cdr, body, stackIndex - WordSize, bindingEnv, &entry));
            return 0;
        }
    }

    constexpr int32_t LabelPlaceholder = 0xdeadbeef;
    int if_(Buffer &buf, ASTNode *condition, ASTNode *onThen, ASTNode *onElse, word stackIndex, const Env *varEnv)
    {
        _(expr(buf, condition, stackIndex, varEnv));
        Emit::cmpRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::encodeBool(false)));
        auto onElsePos = Emit::jcc(buf, Emit::Equal, LabelPlaceholder);
        _(expr(buf, onThen, stackIndex, varEnv));
        auto endPos = Emit::jmp(buf, LabelPlaceholder);
        Emit::backpatchImm32(buf, onElsePos);
        _(expr(buf, onElse, stackIndex, varEnv));
        Emit::backpatchImm32(buf, endPos);
        return 0;
    }

    constexpr Emit::Register HeapPointer = Emit::Rsi;

    int cons(Buffer &buf, ASTNode *car, ASTNode *cdr, word stackIndex, const Env *varEnv)
    {
        // Compile and store car on the stack
        _(expr(buf, car, stackIndex, varEnv));
        Emit::storeIndirectReg(buf, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)}, Emit::Rax);
        // Compile and store cdr
        _(expr(buf, cdr, stackIndex - WordSize, varEnv));
        Emit::storeIndirectReg(buf, Emit::Indirect{HeapPointer, Objects::CdrOffset}, Emit::Rax);
        Emit::loadRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)});
        Emit::storeIndirectReg(buf, Emit::Indirect{HeapPointer, Objects::CarOffset}, Emit::Rax);
        // Store tagged pointer in rax
        Emit::movRegReg(buf, Emit::Rax, HeapPointer);
        Emit::orRegImm8(buf, Emit::Rax, Objects::PairTag);
        // bump the heap pointer
        Emit::addRegImm32(buf, HeapPointer, Objects::PairSize);
        return 0;
    }

    int call(Buffer &buf, ASTNode *callable, ASTNode *args, word stackIndex, const Env *varEnv)
    {
        // assert(args->asPair()->cdr == ASTNode::nil() && "Only unary function calls supported");
        if (callable->isSymbol())
        {
            auto symbol = callable->asSymbol();
            if (symbol->str == "add1")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::addRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::encodeInteger(1)));
                return 0;
            }
            else if (symbol->str == "sub1")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::addRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::encodeInteger(-1)));
                return 0;
            }
            else if (symbol->str == "integer->char")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::shlRegImm8(buf, Emit::Rax, Objects::CharShift - Objects::IntegerShift);
                Emit::orRegImm8(buf, Emit::Rax, static_cast<uint8_t>(Objects::CharTag));
                return 0;
            }
            else if (symbol->str == "char->integer")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::shrRegImm8(buf, Emit::Rax, Objects::CharShift - Objects::IntegerShift);
                return 0;
            }
            else if (symbol->str == "nil?")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                compareInt32(buf, static_cast<int32_t>(Objects::nil()));
                return 0;
            }
            else if (symbol->str == "zero?")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                compareInt32(buf, static_cast<int32_t>(Objects::encodeInteger(0)));
                return 0;
            }
            else if (symbol->str == "not")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                compareInt32(buf, static_cast<int32_t>(Objects::encodeBool(false)));
                return 0;
            }
            else if (symbol->str == "integer?")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::andRegImm8(buf, Emit::Rax, Objects::IntegerMask);
                compareInt32(buf, Objects::IntegerTag);
                return 0;
            }
            else if (symbol->str == "boolean?")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::andRegImm8(buf, Emit::Rax, Objects::BoolTag);
                compareInt32(buf, Objects::BoolTag);
                return 0;
            }
            else if (symbol->str == "+")
            {
                _(expr(buf, operand2(args), stackIndex, varEnv));
                Emit::storeIndirectReg(buf, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)}, Emit::Rax);
                _(expr(buf, operand1(args), stackIndex - WordSize, varEnv));
                Emit::addRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)});
                return 0;
            }
            else if (symbol->str == "-")
            {
                _(expr(buf, operand2(args), stackIndex, varEnv));
                Emit::storeIndirectReg(buf, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)}, Emit::Rax);
                _(expr(buf, operand1(args), stackIndex - WordSize, varEnv));
                Emit::subRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)});
                return 0;
            }
            else if (symbol->str == "=")
            {
                _(expr(buf, operand2(args), stackIndex, varEnv));
                Emit::storeIndirectReg(buf, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)}, Emit::Rax);
                _(expr(buf, operand1(args), stackIndex - WordSize, varEnv));
                Emit::cmpRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)});
                Emit::movRegImm32(buf, Emit::Rax, 0);
                Emit::setccImm8(buf, Emit::Equal, Emit::Al);
                Emit::shlRegImm8(buf, Emit::Rax, Objects::BoolShift);
                Emit::orRegImm8(buf, Emit::Rax, Objects::BoolTag);
                return 0;
            }
            else if (symbol->str == "<")
            {
                _(expr(buf, operand2(args), stackIndex, varEnv));
                Emit::storeIndirectReg(buf, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)}, Emit::Rax);
                _(expr(buf, operand1(args), stackIndex - WordSize, varEnv));
                Emit::cmpRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(stackIndex)});
                Emit::movRegImm32(buf, Emit::Rax, 0);
                Emit::setccImm8(buf, Emit::Less, Emit::Al);
                Emit::shlRegImm8(buf, Emit::Rax, Objects::BoolShift);
                Emit::orRegImm8(buf, Emit::Rax, Objects::BoolTag);
                return 0;
            }
            else if (symbol->str == "let")
            {
                return let(buf, operand1(args), operand2(args), stackIndex,
                           varEnv,  // binding env.
                           varEnv); // body env.
            }
            else if (symbol->str == "if")
            {
                return if_(buf, operand1(args), // condition
                           operand2(args),      // on true
                           operand3(args),      // on false
                           stackIndex, varEnv);
            }
            else if (symbol->str == "cons")
            {
                return cons(buf,
                            operand1(args), // car
                            operand2(args), // cdr,
                            stackIndex,
                            varEnv);
            }
            else if (symbol->str == "car")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::loadRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rax, static_cast<int8_t>(Objects::CarOffset - Objects::PairTag)});
                return 0;
            }
            else if (symbol->str == "cdr")
            {
                _(expr(buf, operand1(args), stackIndex, varEnv));
                Emit::loadRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rax, static_cast<int8_t>(Objects::CdrOffset - Objects::PairTag)});
                return 0;
            }
        }
        assert(false && "unexpected call type");
        return -1;
    }

    int expr(Buffer &buf, ASTNode *node, word stackIndex, const Env *varEnv)
    {
        if (node->isInteger())
        {
            auto value = node->getInteger();
            Emit::movRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::encodeInteger(value)));
            return 0;
        }
        else if (node->isChar())
        {
            auto value = node->getChar();
            Emit::movRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::encodeChar(value)));
            return 0;
        }
        else if (node->isBool())
        {
            auto value = node->getBool();
            Emit::movRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::encodeBool(value)));
            return 0;
        }
        else if (node->isNil())
        {
            Emit::movRegImm32(buf, Emit::Rax, static_cast<int32_t>(Objects::nil()));
            return 0;
        }
        else if (node->isPair())
        {
            auto pair = node->asPair();
            return call(buf, pair->car, pair->cdr, stackIndex, varEnv);
        }
        else if (node->isSymbol())
        {
            auto symbol = node->asSymbol()->str;
            if (auto val = varEnv->find(symbol))
            {
                Emit::loadRegIndirect(buf, Emit::Rax, Emit::Indirect{Emit::Rbp, static_cast<int8_t>(*val)});
                return 0;
            }
            return -1;
        }
        assert(0 && "Unexpected node type");
        return -1;
    }

    int function(Buffer &buf, ASTNode *node)
    {
        buf.writeArray(FunctionPrologue, sizeof(FunctionPrologue));
        _(expr(buf, node, -WordSize, nullptr));
        buf.writeArray(FunctionEpilogue, sizeof(FunctionEpilogue));

        return 0;
    }
#undef _
} // namespace Compile

namespace Reader
{
    struct Reader
    {
        void advance()
        {
            ++pos;
        }
        char next()
        {
            advance();
            return input[pos];
        }

        char peek() const
        {
            return input[pos + 1];
        }

        char skipWS()
        {
            char c = '\0';
            for (c = input[pos]; std::isspace(c); c = next())
            {
            }
            return c;
        }

        ASTNode *readInteger(int sign)
        {
            char c = '\0';
            word result = 0;
            for (char c = input[pos]; isdigit(c); c = next())
            {
                result *= 10;
                result += c - '0';
            }
            return ASTNode::newInteger(sign * result);
        }

        static bool startsSymbol(char c)
        {
            switch (c)
            {
            case '+':
            case '-':
            case '*':
            case '>':
            case '=':
            case '?':
                return true;
            default:
                return isalpha(c);
            }
        }

        static bool isSymbolChar(char c)
        {
            return startsSymbol(c) || std::isdigit(c);
        }

        ASTNode *readSymbol()
        {
            constexpr word ATOM_MAX = 32;
            char buf[ATOM_MAX + 1]; // +1 for NUL
            word length = 0;
            for (length = 0; length < ATOM_MAX && isSymbolChar(input[pos]); length++)
            {
                buf[length] = input[pos];
                advance();
            }
            buf[length] = '\0';
            return ASTNode::newSymbol(buf);
        }

        ASTNode *readChar()
        {
            char c = input[pos];
            if (c == '\'')
            {
                return ASTNode::error();
            }
            advance();
            if (input[pos] != '\'')
            {
                return ASTNode::error();
            }
            advance();
            return ASTNode::newChar(c);
        }

        ASTNode *readList()
        {
            char c = skipWS();
            if (c == ')')
            {
                advance();
                return ASTNode::nil();
            }
            ASTNode *car = readRec();
            assert(car != ASTNode::error());
            ASTNode *cdr = readList();
            assert(cdr != ASTNode::error());
            return ASTNode::newPair(car, cdr);
        }

        ASTNode *readRec()
        {
            char c = skipWS();
            if (std::isdigit(c))
            {
                return readInteger(+1);
            }
            if (c == '+' && std::isdigit(peek()))
            {
                advance();
                return readInteger(+1);
            }
            if (c == '-' && std::isdigit(peek()))
            {
                advance();
                return readInteger(-1);
            }
            if (startsSymbol(c))
            {
                return readSymbol();
            }
            if (c == '\'')
            {
                advance();
                return readChar();
            }
            if (c == '#' && peek() == 't')
            {
                advance();
                advance();
                return ASTNode::newBool(true);
            }
            if (c == '#' && peek() == 'f')
            {
                advance();
                advance();
                return ASTNode::newBool(false);
            }
            if (c == '(')
            {
                advance();
                return readList();
            }
            return ASTNode::error();
        }

        std::string input;
        word pos;
    };
    std::unique_ptr<ASTNode, decltype(&heapFree)> read(const std::string &input)
    {
        return std::unique_ptr<ASTNode, decltype(&heapFree)>{
            Reader{input, (word)0}.readRec(),
            &heapFree};
    }

} // namespace Reader