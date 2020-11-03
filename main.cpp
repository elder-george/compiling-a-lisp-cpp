#include <iostream>
#include <ios>
#include <iomanip>
#include <string>

#include <fmt/ostream.h>

#include "alisp.h"

std::string format_node(const ASTNode *node)
{
    if (node->isInteger())
    {
        return std::to_string(node->getInteger());
    }
    else if (node->isChar())
    {
        return std::string("'") + node->getChar() + "'";
    }
    else if (node->isBool())
    {
        return node->getBool() ? "#t" : "#f";
    }
    else if (node->isSymbol())
    {
        return std::string("'") + node->asSymbol()->str;
    }
    else if (node->isPair())
    {
        auto pair = node->asPair();
        return std::string("(cons ") + format_node(pair->car) + " " + format_node(pair->cdr) + ")";
    }
    assert(false);
    return {};
}

int repl()
{
    using namespace std;
    do
    {
        fmt::print("lisp>");
        std::string line;
        if (!getline(cin, line) || line.size() == 0)
        {
            fmt::print("Good bye\n");
            break;
        }
        // parse the line
        auto node = Reader::read(std::move(line));
        if (node->isError())
        {
            fmt::print(cerr, "Parse error!\n");
            continue;
        }
        // Compile the line
        Buffer buf;
        auto result = Compile::function(buf, node.get());
        if (result != 0)
        {
            fmt::print(cerr, "Compile error\n");
            continue;
        }
        auto code = buf.freeze();
        uword heap[256];
        auto executionResult = code.toFunc<ASTNode *(uword *)>()(heap);
        fmt::print("Result = {}\n", format_node(executionResult));
    } while (true);
    return 0;
}

int main()
{
    std::ios::sync_with_stdio(false);
    return repl();
}