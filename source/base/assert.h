/**
 *  @note This file is part of Empirical, https://github.com/devosoft/Empirical
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2016-2017
 *
 *  @file assert.h
 *  @brief A more dynamic replacement for standard library asserts.
 *  @note Status: RELEASE
 *
 *  A replacement for the system-level assert.h, called "emp_assert"
 *  Added functionality:
 *   - If compiled with Emscripten, will provide pop-up alerts in a web browser.
 *   - emp_assert can take additional arguments.  If the assert is triggered, those extra
 *     arguments will be evaluated and printed.
 *   - if NDEBUG -or- EMP_NDEBUG is defined, the expression in emp_assert() is not evaluated.
 *   - if EMP_TDEBUG is defined, emp_assert() goes into test mode and records failures, but
 *     does not abort.  (useful for unit tests of asserts)
 *
 *  Example:
 *
 *     int a = 6;
 *     emp_assert(a==5, a);
 *
 *  When compiled in debug mode (i.e. without the -DNDEBUG flag), this will trigger an assertion
 *  error and print the value of a.
 */

#ifndef EMP_ASSERT_H
#define EMP_ASSERT_H

#include <iostream>
#include <string>
#include <sstream>

#include "macros.h"

/// @cond DEFINES

/// If we are in emscripten, make sure to include the header.
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

/// NDEBUG and TDEBUG should trigger their EMP equivilents.
#ifdef NDEBUG
#define EMP_NDEBUG
#endif

#ifdef TDEBUG
#define EMP_TDEBUG
#endif


/// It's normally not possible to put an assert in a constexpr function because printing is not
/// available at compile time.  The "emp_constexpr" macro can replace "constexpr";
/// emp_constexpr ignores constexpr in debug mode; regular assers disappear outside of debug mode.
/// Of course, this requires testing with NDEBUG turned on to make sure compilation works!

#ifdef EMP_NDEBUG
#define emp_constexpr constexpr
#else
#define emp_constexpr
#endif


/// Turn off all asserts in EMP_NDEBUG
#ifdef EMP_NDEBUG
namespace emp {
  constexpr bool assert_on = false;
}

/// Ideally, this assert should use the expression (to prevent compiler error), but should not
/// generate any assembly code.  For now, just make it blank (other options commented out)
#define emp_assert(...)
// #define emp_assert(EXPR) ((void) sizeof(EXPR) )
// #define emp_assert(EXPR, ...) { constexpr bool __emp_assert_tmp = false && (EXPR); (void) __emp_assert_tmp; }

#elif defined(EMP_TDEBUG)           // EMP_NDEBUG not set, but EMP_TDEBUG is!

namespace emp {
  constexpr bool assert_on = true;
  struct AssertFailInfo {
    std::string filename;
    int line_num;
    std::string error;
  };
  AssertFailInfo assert_fail_info;
  bool assert_last_fail = false;
}

// Generate a pop-up alert in a web browser if an assert is tripped.
#define emp_assert_tdebug_impl(EXPR) emp_assert_tdebug_impl2(EXPR)

#define emp_assert_tdebug_impl2(EXPR)                                   \
  do {                                                                  \
    if ( !(EXPR) ) {                                                    \
      emp::assert_last_fail = true;                                     \
      emp::assert_fail_info.filename = __FILE__;                        \
      emp::assert_fail_info.line_num = __LINE__;                        \
      emp::assert_fail_info.error = EMP_STRINGIFY(EXPR);                \
    }                                                                   \
    else {                                                              \
      emp::assert_last_fail = false;                                    \
    }                                                                   \
  } while (0)

#define emp_assert(...) emp_assert_tdebug_impl( EMP_GET_ARG_1(__VA_ARGS__, ~) )


#elif EMSCRIPTEN  // Neither EMP_NDEBUG nor EMP_TDEBUG set, but compiling with Emscripten

namespace emp {
  constexpr bool assert_on = true;
  static int TripAssert() {
    static int trip_count = 0;
    return ++trip_count;
  }
}

// Generate a pop-up alert in a web browser if an assert it tripped.
#define emp_assert_impl_1(EXPR)                                         \
  if ( !(EXPR) ) {                                                      \
    std::string msg = std::string("Assert Error (In ")                  \
      + std::string(__FILE__)                                           \
      + std::string(" line ") + std::to_string(__LINE__)                \
      + std::string("): ") + std::string(#EXPR) + "\n"                  \
      + emp_assert_var_info.str();                                      \
    if (emp::TripAssert() <= 3)						\
      EM_ASM_ARGS({ msg = Pointer_stringify($0); alert(msg); }, msg.c_str()); \
    abort();                                                            \
  }                                                                     \

#define emp_assert_var(VAR) emp_assert_var_info << #VAR << ": [" << VAR << "]\n";

#define emp_assert_impl_2(EXPR, VAR) emp_assert_var(VAR); emp_assert_impl_1(EXPR)
#define emp_assert_impl_3(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_2(EXPR,__VA_ARGS__)
#define emp_assert_impl_4(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_3(EXPR,__VA_ARGS__)
#define emp_assert_impl_5(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_4(EXPR,__VA_ARGS__)
#define emp_assert_impl_6(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_5(EXPR,__VA_ARGS__)
#define emp_assert_impl_7(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_6(EXPR,__VA_ARGS__)
#define emp_assert_impl_8(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_7(EXPR,__VA_ARGS__)
#define emp_assert_impl_9(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_8(EXPR,__VA_ARGS__)
#define emp_assert_impl_10(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_9(EXPR,__VA_ARGS__)
#define emp_assert_impl_11(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_10(EXPR,__VA_ARGS__)
#define emp_assert_impl_12(EXPR, VAR, ...) emp_assert_var(VAR); emp_assert_impl_11(EXPR,__VA_ARGS__)

#define emp_assert(...)                                                 \
  do {                                                                  \
    std::stringstream emp_assert_var_info;                              \
    EMP_ASSEMBLE_MACRO(emp_assert_impl_, __VA_ARGS__) \
  } while(0)


#else // We ARE in DEBUG, but NOT in EMSCRIPTEN

namespace emp {
  constexpr bool assert_on = true;

  /// Base case for assert_print...
  void assert_print() { ; }

  /// Print out information about the next variable and recurse...
  template <typename T, typename... EXTRA>
  void assert_print(std::string name, T && val, EXTRA &&... extra) {
    std::cerr << name << ": [" << val << "]" << std::endl;
    assert_print(std::forward<EXTRA>(extra)...);
  }

  template <typename... EXTRA>
  bool assert_trigger(std::string filename, size_t line, std::string expr, bool, EXTRA &&... extra) {
    std::cerr << "Assert Error (In " << filename << " line " << line
              <<  "): " << expr << std::endl;
    assert_print(std::forward<EXTRA>(extra)...);
    return true;
  }
}

#define emp_assert_TO_PAIR(X) EMP_STRINGIFY(X) , X

/// @endcond

/// Require a specified condition to be true.  If it is false, immediately halt execution.
/// Note: If NDEBUG is defined, emp_assert() will not do anything.
#define emp_assert(...)                                                                          \
  do {                                                                                           \
    !(EMP_GET_ARG_1(__VA_ARGS__, ~)) &&                                                          \
    emp::assert_trigger(__FILE__, __LINE__, EMP_WRAP_ARGS(emp_assert_TO_PAIR, __VA_ARGS__) ) &&  \
    (abort(), false);                                                                            \
  } while(0)

/// @cond DEFINES

#endif // NDEBUG


#endif // Include guard

/// @endcond
