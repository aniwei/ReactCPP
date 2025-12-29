#include "ReactSymbols.h"

namespace react::shared {

std::string_view getSymbolName(ReactSymbol symbol) {
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

} // namespace react::shared
