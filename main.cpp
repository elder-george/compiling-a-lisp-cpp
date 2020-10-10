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
            fmt::print("Good bye");
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
        auto result = Compile::expr(buf, node.get(), -WordSize, nullptr);
        if (result != 0)
        {
            fmt::print(cerr, "Compile error\n");
            continue;
        }

        // Print the assembled code
        for (size_t i = 0; i < buf._buf.size(); i++)
        {
            fmt::print(cerr, "{:02x} ", buf._buf[i]);
        }
        cerr << "\n";
    } while (true);
    return 0;
}

int main()
{
    std::ios::sync_with_stdio(false);
    return repl();
}