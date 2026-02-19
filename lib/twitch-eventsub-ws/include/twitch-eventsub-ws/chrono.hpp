#pragma once

#include <boost/json.hpp>
#include <boost/version.hpp>

#include <chrono>
#include <version>

#if __cpp_lib_chrono < 201907L
#    define CHATTERINO_USING_HOWARD_HINNANTS_DATE
#endif

namespace chatterino::eventsub::lib {

struct AsISO8601 {
};

boost::json::result_for<std::chrono::system_clock::time_point,
                        boost::json::value>::type
    tag_invoke(
        boost::json::try_value_to_tag<std::chrono::system_clock::time_point>,
        const boost::json::value &jvRoot, const AsISO8601 &);

inline boost::json::result_for<std::chrono::system_clock::time_point,
                               boost::json::value>::type
try_value_to_iso8601(const boost::json::value &jvRoot)
{
    return tag_invoke(
        boost::json::try_value_to_tag<std::chrono::system_clock::time_point>{},
        jvRoot, AsISO8601{});
}

}  // namespace chatterino::eventsub::lib

#if BOOST_VERSION < 108500
namespace boost::json {

inline result_for<std::chrono::system_clock::time_point, value>::type
try_value_to(const value &jvRoot, const chatterino::eventsub::lib::AsISO8601 &tag)
{
    return chatterino::eventsub::lib::tag_invoke(
        try_value_to_tag<std::chrono::system_clock::time_point>{}, jvRoot,
        tag);
}

}  // namespace boost::json
#endif
