#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace mihini {

// ---------------------------------------------------------------------------
// DataStoreError
// ---------------------------------------------------------------------------
class DataStoreError : public std::runtime_error {
public:
    explicit DataStoreError(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------
// The single value type carried by the data store. It is a std::variant whose
// alternatives line up, in order, with the SWI dset type codes:
//
//   index 0 -> Nil      (std::monostate)  -> SWI_DSET_NIL
//   index 1 -> Integer  (std::int64_t)    -> SWI_DSET_INTEGER
//   index 2 -> Float    (double)          -> SWI_DSET_FLOAT
//   index 3 -> String   (std::string)     -> SWI_DSET_STRING
//   index 4 -> Bool     (bool)            -> SWI_DSET_BOOL
//
// Keeping the index order in sync with the C enum lets the EMP backend convert
// between the two in O(1) with a plain cast.
using Value = std::variant<std::monostate, std::int64_t, double, std::string, bool>;

enum class ValueType {
    Nil = 0,
    Integer = 1,
    Float = 2,
    String = 3,
    Bool = 4,
};

inline ValueType type_of(const Value& value)
{
    return static_cast<ValueType>(value.index());
}

inline bool is_nil(const Value& value)
{
    return std::holds_alternative<std::monostate>(value);
}

// A set of path -> value pairs. Mirrors the "varmap" the Mihini device tree
// hands to a notification callback.
using ChangeMap = std::map<std::string, Value>;

// Callback invoked when one or more registered variables change. Receives every
// changed registered variable plus any passive variables, as path/value pairs.
using NotifyCallback = std::function<void(const ChangeMap&)>;

inline std::ostream& operator<<(std::ostream& os, const Value& value)
{
    std::visit(
        [&os](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                os << "nil";
            } else if constexpr (std::is_same_v<T, bool>) {
                os << (v ? "true" : "false");
            } else {
                os << v;
            }
        },
        value);
    return os;
}

} // namespace mihini
