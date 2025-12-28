/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @source reactjs/packages/shared/ReactSymbols.js
 * 
 * ReactSymbols - React 元素类型标识符
 * 
 * 本文件与 ReactJS 的 ReactSymbols.js 完全 1:1 对应
 * 
 * 在 C++ 中，我们使用 constexpr 字符串和整数标识符来替代 JS 的 Symbol
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include "ReactFeatureFlags.h"

namespace react::shared {

// =============================================================================
// Symbol 实现
// =============================================================================

/**
 * 在 C++ 中，我们使用 constexpr 整数来模拟 Symbol.for() 的行为
 * 每个 "symbol" 是一个唯一的常量，可以用于快速比较
 */

// Symbol 类型定义
using ReactSymbol = uint32_t;

// 生成唯一 Symbol 值的编译期函数
constexpr ReactSymbol makeSymbol(const char* name) {
    // 简单的编译期哈希（FNV-1a）
    uint32_t hash = 2166136261u;
    for (const char* p = name; *p; ++p) {
        hash ^= static_cast<uint32_t>(*p);
        hash *= 16777619u;
    }
    return hash;
}

// =============================================================================
// React Element Type Symbols
// @source:16-50
// =============================================================================

// @source:16 - REACT_LEGACY_ELEMENT_TYPE: Symbol.for('react.element')
constexpr ReactSymbol REACT_LEGACY_ELEMENT_TYPE = makeSymbol("react.element");

// @source:17-19 - REACT_ELEMENT_TYPE
// 根据 renameElementSymbol 标志选择不同的 symbol
constexpr ReactSymbol REACT_ELEMENT_TYPE = 
    renameElementSymbol 
        ? makeSymbol("react.transitional.element")
        : REACT_LEGACY_ELEMENT_TYPE;

// @source:20 - REACT_PORTAL_TYPE: Symbol.for('react.portal')
constexpr ReactSymbol REACT_PORTAL_TYPE = makeSymbol("react.portal");

// @source:21 - REACT_FRAGMENT_TYPE: Symbol.for('react.fragment')
constexpr ReactSymbol REACT_FRAGMENT_TYPE = makeSymbol("react.fragment");

// @source:22 - REACT_STRICT_MODE_TYPE: Symbol.for('react.strict_mode')
constexpr ReactSymbol REACT_STRICT_MODE_TYPE = makeSymbol("react.strict_mode");

// @source:23 - REACT_PROFILER_TYPE: Symbol.for('react.profiler')
constexpr ReactSymbol REACT_PROFILER_TYPE = makeSymbol("react.profiler");

// @source:24 - REACT_CONSUMER_TYPE: Symbol.for('react.consumer')
constexpr ReactSymbol REACT_CONSUMER_TYPE = makeSymbol("react.consumer");

// @source:25 - REACT_CONTEXT_TYPE: Symbol.for('react.context')
constexpr ReactSymbol REACT_CONTEXT_TYPE = makeSymbol("react.context");

// @source:26 - REACT_FORWARD_REF_TYPE: Symbol.for('react.forward_ref')
constexpr ReactSymbol REACT_FORWARD_REF_TYPE = makeSymbol("react.forward_ref");

// @source:27 - REACT_SUSPENSE_TYPE: Symbol.for('react.suspense')
constexpr ReactSymbol REACT_SUSPENSE_TYPE = makeSymbol("react.suspense");

// @source:28-30 - REACT_SUSPENSE_LIST_TYPE: Symbol.for('react.suspense_list')
constexpr ReactSymbol REACT_SUSPENSE_LIST_TYPE = makeSymbol("react.suspense_list");

// @source:31 - REACT_MEMO_TYPE: Symbol.for('react.memo')
constexpr ReactSymbol REACT_MEMO_TYPE = makeSymbol("react.memo");

// @source:32 - REACT_LAZY_TYPE: Symbol.for('react.lazy')
constexpr ReactSymbol REACT_LAZY_TYPE = makeSymbol("react.lazy");

// @source:33 - REACT_SCOPE_TYPE: Symbol.for('react.scope')
constexpr ReactSymbol REACT_SCOPE_TYPE = makeSymbol("react.scope");

// @source:34 - REACT_ACTIVITY_TYPE: Symbol.for('react.activity')
constexpr ReactSymbol REACT_ACTIVITY_TYPE = makeSymbol("react.activity");

// @source:35-37 - REACT_LEGACY_HIDDEN_TYPE: Symbol.for('react.legacy_hidden')
constexpr ReactSymbol REACT_LEGACY_HIDDEN_TYPE = makeSymbol("react.legacy_hidden");

// @source:38-40 - REACT_TRACING_MARKER_TYPE: Symbol.for('react.tracing_marker')
constexpr ReactSymbol REACT_TRACING_MARKER_TYPE = makeSymbol("react.tracing_marker");

// @source:42-44 - REACT_MEMO_CACHE_SENTINEL: Symbol.for('react.memo_cache_sentinel')
constexpr ReactSymbol REACT_MEMO_CACHE_SENTINEL = makeSymbol("react.memo_cache_sentinel");

// @source:46 - REACT_POSTPONE_TYPE: Symbol.for('react.postpone')
constexpr ReactSymbol REACT_POSTPONE_TYPE = makeSymbol("react.postpone");

// @source:48-50 - REACT_VIEW_TRANSITION_TYPE: Symbol.for('react.view_transition')
constexpr ReactSymbol REACT_VIEW_TRANSITION_TYPE = makeSymbol("react.view_transition");

// =============================================================================
// Symbol 名称映射（用于调试）
// =============================================================================

/**
 * 获取 Symbol 的字符串名称（用于调试输出）
 */
inline std::string_view getSymbolName(ReactSymbol symbol) {
    if (symbol == REACT_LEGACY_ELEMENT_TYPE) return "react.element";
    if (symbol == REACT_ELEMENT_TYPE && renameElementSymbol) return "react.transitional.element";
    if (symbol == REACT_PORTAL_TYPE) return "react.portal";
    if (symbol == REACT_FRAGMENT_TYPE) return "react.fragment";
    if (symbol == REACT_STRICT_MODE_TYPE) return "react.strict_mode";
    if (symbol == REACT_PROFILER_TYPE) return "react.profiler";
    if (symbol == REACT_CONSUMER_TYPE) return "react.consumer";
    if (symbol == REACT_CONTEXT_TYPE) return "react.context";
    if (symbol == REACT_FORWARD_REF_TYPE) return "react.forward_ref";
    if (symbol == REACT_SUSPENSE_TYPE) return "react.suspense";
    if (symbol == REACT_SUSPENSE_LIST_TYPE) return "react.suspense_list";
    if (symbol == REACT_MEMO_TYPE) return "react.memo";
    if (symbol == REACT_LAZY_TYPE) return "react.lazy";
    if (symbol == REACT_SCOPE_TYPE) return "react.scope";
    if (symbol == REACT_ACTIVITY_TYPE) return "react.activity";
    if (symbol == REACT_LEGACY_HIDDEN_TYPE) return "react.legacy_hidden";
    if (symbol == REACT_TRACING_MARKER_TYPE) return "react.tracing_marker";
    if (symbol == REACT_MEMO_CACHE_SENTINEL) return "react.memo_cache_sentinel";
    if (symbol == REACT_POSTPONE_TYPE) return "react.postpone";
    if (symbol == REACT_VIEW_TRANSITION_TYPE) return "react.view_transition";
    return "unknown";
}

// =============================================================================
// Iterator 相关
// @source:52-67
// =============================================================================

/**
 * 检查对象是否可迭代
 * 
 * @source:53-64 - getIteratorFn
 * 
 * 在 C++ 中，这需要通过模板和 SFINAE 来实现
 * 这里提供一个占位符，实际实现需要结合 JSI
 */

// ASYNC_ITERATOR symbol
// @source:66
constexpr ReactSymbol ASYNC_ITERATOR = makeSymbol("Symbol.asyncIterator");

} // namespace react::shared
