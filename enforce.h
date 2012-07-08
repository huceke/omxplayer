#pragma once

#include <stdexcept>

#define STRINGIZE(something) STRINGIZE_HELPER(something) 
#define STRINGIZE_HELPER(something) #something
#define ENFORCE(exp) \
  enforce((exp), "Enforcement failed in " __FILE__ "(" STRINGIZE(__LINE__) ")")

static void enforce(bool condition, const char* msg) {
  if (!condition)
    throw std::runtime_error(msg);
}