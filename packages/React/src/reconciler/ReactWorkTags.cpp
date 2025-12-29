#include "ReactWorkTags.h"

namespace react::reconciler {

const char* getWorkTagName(WorkTag tag) {
  switch (tag) {
    case FunctionComponent: return "FunctionComponent";
    case ClassComponent: return "ClassComponent";
    case HostRoot: return "HostRoot";
    case HostPortal: return "HostPortal";
    case HostComponent: return "HostComponent";
    case HostText: return "HostText";
    case Fragment: return "Fragment";
    case Mode: return "Mode";
    case ContextConsumer: return "ContextConsumer";
    case ContextProvider: return "ContextProvider";
    case ForwardRef: return "ForwardRef";
    case Profiler: return "Profiler";
    case SuspenseComponent: return "SuspenseComponent";
    case MemoComponent: return "MemoComponent";
    case SimpleMemoComponent: return "SimpleMemoComponent";
    case LazyComponent: return "LazyComponent";
    case IncompleteClassComponent: return "IncompleteClassComponent";
    case DehydratedFragment: return "DehydratedFragment";
    case SuspenseListComponent: return "SuspenseListComponent";
    case ScopeComponent: return "ScopeComponent";
    case OffscreenComponent: return "OffscreenComponent";
    case LegacyHiddenComponent: return "LegacyHiddenComponent";
    case CacheComponent: return "CacheComponent";
    case TracingMarkerComponent: return "TracingMarkerComponent";
    case HostHoistable: return "HostHoistable";
    case HostSingleton: return "HostSingleton";
    case IncompleteFunctionComponent: return "IncompleteFunctionComponent";
    case Throw: return "Throw";
    case ViewTransitionComponent: return "ViewTransitionComponent";
    case ActivityComponent: return "ActivityComponent";
    default: return "Unknown";
  }
}

} // namespace react::reconciler
