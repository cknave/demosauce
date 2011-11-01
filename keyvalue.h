/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef KEYVALUE_H
#define KEYVALUE_H

#include <string>
#include <boost/lexical_cast.hpp>

#include "logror.h"

namespace {

const char* cstr(std::string& s)
{
    return s.empty() ? "{}" : s.c_str();
}

const char* cstr(const char* s)
{
    return s && s[0] != 0 ? s : "{}";
}

bool get_value_impl(std::string data, std::string key, std::string& value)
{
    if (data.empty() || key.empty()) {
        return false;
    }
    size_t key_start = data.find(key);
    if (key_start > 0 && data[key_start - 1] != '\n') {
        return false;
    }
    size_t value_start = key_start + key.size() + 1;
    if (value_start >= data.size() || data[value_start - 1] != '=') {
        return false;
    }
    size_t value_end = data.find('\n', value_start);
    value = data.substr(value_start, value_end - value_start);
    return true;
}

}

inline std::string get_value(std::string data, std::string key, std::string fallback_value)
{
    std::string value = fallback_value;
    get_value_impl(data, key, value);
    LOG_DEBUG("[get_value] %s = %s", cstr(key), cstr(value));
    return value;
}

inline std::string get_value(std::string data, std::string key, const char* fallback_value)
{
    return get_value(data, key, std::string(fallback_value));
}

inline bool get_value(std::string data, std::string key, bool fallback_value)
{
    std::string strv;
    bool value = fallback_value;
    if (get_value_impl(data, key, strv))
        value = (strv == "true" || strv == "True" || strv == "1");
    LOG_DEBUG("[get_value] %s = %s", cstr(key), value ? "true" : "false");
    return value;
}

template <typename T> T get_value(std::string data, std::string key, T fallback_value)
{
    std::string value_str;
    T value = fallback_value;
    if (get_value_impl(data, key, value_str)) {
        try {
            value = boost::lexical_cast<T>(value_str);
        } catch (boost::bad_lexical_cast&) {}
    }
#ifndef NDEBUG
    try {
        value_str = boost::lexical_cast<std::string>(value);
        LOG_DEBUG("[get_value] %s = %s", cstr(key), cstr(value_str));
    } catch (boost::bad_lexical_cast&) {}
#endif
    return value;
}

#endif
