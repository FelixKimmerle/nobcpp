#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using FlagList = std::vector<std::string>;
using ProfileDimension = std::map<std::string, FlagList>;

FlagList get_flags(const std::vector<std::pair<const ProfileDimension&, std::string>>& selections)
{
    FlagList result;
    for (const auto& [dimension, key] : selections)
    {
        auto it = dimension.find(key);
        if (it != dimension.end())
        {
            result.insert(result.end(), it->second.begin(), it->second.end());
        }
    }
    return result;
}

std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
    {
        elems.push_back(item);
    }
    return elems;
}

int main()
{
    ProfileDimension build_type = {{"debug", {"-g", "-O0"}}, {"release", {"-O3"}}};
    ProfileDimension asan = {{"asan_on", {"-fsanitize=address"}}, {"asan_off", {}}};

    ProfileDimension dummy = {{"dummy_on", {"-lol"}}, {"dummy_off", {"-jooo"}}};

    std::vector<std::pair<std::string, ProfileDimension>> dimensions = {
        {"build_type", build_type}, {"asan", asan}, {"dummy", dummy}};

    std::string query = "asan_on/dummy_on";
    auto values = split(query, '/');

    std::vector<std::pair<const ProfileDimension&, std::string>> selections;
    for (size_t i = 0; i < values.size() && i < dimensions.size(); ++i)
    {
        selections.emplace_back(dimensions[i].second, values[i]);
    }

    auto flags = get_flags(selections);

    for (const auto& flag : flags)
    {
        std::cout << flag << " ";
    }
    std::cout << std::endl;
}
