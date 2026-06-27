#pragma once

#include <algorithm>
#include <vector>

namespace util {

template <typename T>
inline auto sort_and_deduplicate(std::vector<T>& values) -> void {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename T>
inline auto sorted_unique(std::vector<T> values) -> std::vector<T> {
    sort_and_deduplicate(values);
    return values;
}

}  // namespace util
