#ifndef _H_KEYVALUE_
#define _H_KEYVALUE_

#include <string>
#include <boost/lexical_cast.hpp>

#include "logror.h"

namespace {

bool get_value_impl(std::string data, std::string key, std::string& value)
{
    if (data.empty() || key.empty())
        return false;
    size_t key_start = data.find(key);
    if (key_start > 0 && data[key_start - 1] != '\n')
        return false;
    size_t value_start = key_start + key.size() + 1;
    if (value_start >= data.size() || data[value_start - 1] != ':')
        return false;
    size_t value_end = data.find('\n', value_start);
    value = data.substr(value_start, value_end - value_start);
    return true;
}

}

inline std::string get_value(std::string data, std::string key, std::string fallback_value)
{
    std::string value;
    if (!get_value_impl(data, key, value))
        return fallback_value;
    LOG_DEBUG("get_value([data], %1%, %2%): %3%"), key, fallback_value, value;
    return value;
}

inline std::string get_value(std::string data, std::string key, const char* fallback_value)
{
    return get_value(data, key, std::string(fallback_value));
}

template <typename T> T get_value(std::string data, std::string key, T fallback_value)
{
    std::string value_str;
    if (!get_value_impl(data, key, value_str))
        return fallback_value;
    T value = fallback_value;
    try
    {
        value = boost::lexical_cast<T>(value_str);
    }
    catch (boost::bad_lexical_cast&) {}
    LOG_DEBUG("get_value([data], %1%, %2%): %3%"), key, fallback_value, value;
    return value;
}

#endif
