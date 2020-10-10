#include <iostream>
#include <ios>
#include <iomanip>
#include <string>

#include <fmt/ostream.h>

#include "alisp.h"

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
        auto node = Reader::read(line);
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
        fmt::print("Result = {}\n", Objects::decodeInteger(code.toFunc<int()>()()));
    } while (true);
    return 0;
}

int main()
{
    std::ios::sync_with_stdio(false);
    return repl();
}