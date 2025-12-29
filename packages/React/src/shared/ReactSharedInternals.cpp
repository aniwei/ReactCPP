#include "ReactSharedInternals.h"

#include "../runtime/ReactHostRuntime.h"

namespace react {

std::unique_ptr<ReactSharedInternals> ReactSharedInternals::create() {
  return std::unique_ptr<ReactSharedInternals>(new ReactSharedInternals());
}

void ReactSharedInternals::setDispatcher(Dispatcher* dispatcher) {
  client_.H = dispatcher;
}

Dispatcher* ReactSharedInternals::getDispatcher() const {
  return client_.H;
}

Dispatcher& ReactSharedInternals::resolveDispatcher() {
  Dispatcher* dispatcher = client_.H;

#ifdef DEV
  if (dispatcher == nullptr) {
    throw std::runtime_error(
      "Invalid hook call. Hooks can only be called inside of the body of a function component. "
      "This could happen for one of the following reasons:\n"
      "1. You might have mismatching versions of React and the renderer\n"
      "2. You might be breaking the Rules of Hooks\n"
      "3. You might have more than one copy of React in the same app"
    );
  }
#endif

  return *dispatcher;
}

void ReactSharedInternals::setAsyncDispatcher(AsyncDispatcher* dispatcher) {
  client_.A = dispatcher;
}

AsyncDispatcher* ReactSharedInternals::getAsyncDispatcher() const {
  return client_.A;
}

void ReactSharedInternals::setTransition(Transition* transition) {
  client_.T = transition;
}

Transition* ReactSharedInternals::getTransition() const {
  return client_.T;
}

SharedStateClient& ReactSharedInternals::getClientState() {
  return client_;
}

const SharedStateClient& ReactSharedInternals::getClientState() const {
  return client_;
}

SharedStateServer& ReactSharedInternals::getServerState() {
  return server_;
}

const SharedStateServer& ReactSharedInternals::getServerState() const {
  return server_;
}

Dispatcher* getCurrentDispatcher(ReactHostRuntime& hostRuntime) {
  return hostRuntime.getSharedInternals().getDispatcher();
}

Dispatcher& resolveDispatcher(ReactHostRuntime& hostRuntime) {
  return hostRuntime.getSharedInternals().resolveDispatcher();
}

AsyncDispatcher* getCurrentAsyncDispatcher(ReactHostRuntime& hostRuntime) {
  return hostRuntime.getSharedInternals().getAsyncDispatcher();
}

Transition* getCurrentTransition(ReactHostRuntime& hostRuntime) {
  return hostRuntime.getSharedInternals().getTransition();
}

} // namespace react
