#pragma once

namespace react {

void* createCacheInstance();
void retainCache(void* cache);
void releaseCache(void* cache);

} // namespace react
