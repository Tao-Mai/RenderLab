#include <format>
#include <string>
import format_validation;

std::string second();
int main()
{
    return formatValue(std::string{"first"}) == "first" && second() == "second" ? 0 : 1;
}
