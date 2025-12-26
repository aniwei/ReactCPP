#pragma once

// AUTO-GENERATED. DO NOT EDIT BY HAND.
// Source: reactjs/packages/react-reconciler/src/ReactFiberHooks.js

namespace react::transpiled {

inline constexpr const char* kThrowInvalidHookErrorMessage = "Invalid hook call. Hooks can only be called inside of the body of a function component. This could happen for one of the following reasons:\n1. You might have mismatching versions of React and the renderer (such as React DOM)\n2. You might be breaking the Rules of Hooks\n3. You might have more than one copy of React in the same app\nSee https://react.dev/link/invalid-hook-call for tips about how to debug and fix this problem.";
inline constexpr const char* kWarnOnUseFormStateInDevFormat = "ReactDOM.useFormState has been renamed to React.useActionState. Please update %s to use React.useActionState.";
inline constexpr const char* kWarnIfAsyncClientComponentFormat = "%s is an async Client Component. Only Server Components can be async at the moment. This error is often caused by accidentally adding `'use client'` to a module that was originally written for the server.";
inline constexpr const char* kObjectAsyncFunctionTag = "[object AsyncFunction]";
inline constexpr const char* kObjectAsyncGeneratorFunctionTag = "[object AsyncGeneratorFunction]";
inline constexpr const char* kWarnIfAsyncClientComponentUnknownComponent = "An unknown Component";
inline constexpr const char* kWarnIfAsyncClientComponentNamePrefix = "<";
inline constexpr const char* kWarnIfAsyncClientComponentNameSuffix = ">";
inline constexpr const char* kCheckDepsAreArrayDevFormat = "%s received a final argument that is not an array (instead, received `%s`). When specified, the final argument must be an array.";
inline constexpr const char* kWarnOnHookMismatchInDevFormat = "React has detected a change in the order of Hooks called by %s. This will lead to bugs and errors if not fixed. For more information, read the Rules of Hooks: https://react.dev/link/rules-of-hooks\n\n   Previous render            Next render\n   ------------------------------------------------------\n%s   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";

} // namespace react::transpiled
