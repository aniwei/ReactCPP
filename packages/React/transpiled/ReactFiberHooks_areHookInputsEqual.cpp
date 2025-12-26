#include <cmath>
#include <cstddef>
#include <vector>

namespace react::transpiled {

static bool objectIs(double x, double y) {
  if (std::isnan(x) && std::isnan(y)) {
    return true;
  }
  if (x == 0.0 && y == 0.0) {
    return std::signbit(x) == std::signbit(y);
  }
  return x == y;
}

bool areHookInputsEqual(
  const std::vector<double>& nextDeps,
  const std::vector<double>* prevDeps,
  bool __DEV__,
  bool ignorePreviousDependencies) {
  if (__DEV__) {
    if (ignorePreviousDependencies) {
      return false;
    }
  }

  if (prevDeps == nullptr) {
    if (__DEV__) {
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
    }
    return false;
  }

  if (__DEV__) {
    if (nextDeps.size() != prevDeps->size()) {
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
      (void)0;
    }
  }
  for (std::size_t i = 0; i < prevDeps->size() && i < nextDeps.size(); i++) {
    if (objectIs(nextDeps[i], (*prevDeps)[i])) {
      continue;
    }
    return false;
  }
  return true;

}

} // namespace react::transpiled
