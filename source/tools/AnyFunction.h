/**
 *  @note This file is part of Empirical, https://github.com/devosoft/Empirical
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2019
 *
 *  @file  AnyFunction.h
 *  @brief Based on std::function, but with a generic base class.
 *  @note Status: ALPHA
 *
 *  (Built from GenericFunction.h)
 */


#ifndef EMP_ANY_FUNCTION_H
#define EMP_ANY_FUNCTION_H

#include <functional>
#include "../base/assert.h"
#include "../base/Ptr.h"

namespace emp {

  ///  An emp::BaseFunction object can be converted back into the derived type with the
  ///  .Convert<return(args...)>() member function.

  class BaseFunction {
  protected:
  public:
    virtual ~BaseFunction() { ; }

    virtual size_t NumArgs() const = 0;

    /// A generic form of the function call operator; use arg types to determine derived form.
    template <typename RETURN, typename... Ts> auto Call(Ts &&... args);

    /// Test if a function call will succeed before trying it.
    template <typename RETURN, typename... Ts> bool CallOK(Ts &&...);

    /// Test if a function call will succeed before trying it, based only on types.
    template <typename RETURN, typename... Ts> bool CallTypeOK();

    /// A generic form of the function call operator; use arg types to determine derived form.
    template <typename RETURN, typename... Ts> auto operator()(Ts &&... args) {
      return Call<RETURN, Ts...>( std::forward<Ts>(args)... );
    }

    /// Convert this BaseFunction into a derived emp::Function
    template <typename T> auto Convert();

    /// Determine if this BaseFunction can be converted into a derived emp::Function
    template <typename T> bool ConvertOK();
  };


  /// Undefined base type for DerivedFunction, to create an error if a function type is not passed in.
  template <typename... Ts> class DerivedFunction;

  /// Specialized form for proper function types.
  template <typename RETURN, typename... PARAMS>
  class DerivedFunction<RETURN(PARAMS...)> : public BaseFunction {
  protected:
    using fun_t = std::function<RETURN(PARAMS...)>;
    fun_t fun;  ///< The std::function to be called.
  public:
    /// Forward all args to std::function constructor...
    template <typename... Ts>
    DerivedFunction(Ts &&... args) : fun(std::forward<Ts>(args)...) { ; }

    // How many arguments does this function have?
    size_t NumArgs() const override { return sizeof...(PARAMS); };

    /// Forward all args to std::function call.
    template <typename... Ts>
    RETURN Call(Ts &&... args) { return fun(std::forward<Ts>(args)...); }

    /// Forward all args to std::function call.
    template <typename... Ts>
    RETURN operator()(Ts &&... args) { return fun(std::forward<Ts>(args)...); }

    /// Get the std::function to be called.
    const fun_t & GetFunction() const { return fun; }
  };


  /// AnyFunction manages the function pointers to be dynamically handled.
  class AnyFunction {
  private:
    emp::Ptr<BaseFunction> fun = nullptr;
  public:
    AnyFunction() { ; }
    ~AnyFunction() { if (fun) fun.Delete(); }

    void Clear() { if (fun) fun.Delete(); fun = nullptr; }
  };


  /////////////////////////////////////
  //  Member function implementaions.

  template <typename RETURN, typename... Ts>
  auto BaseFunction::Call(Ts &&... args) {
    using fun_t = DerivedFunction<RETURN(Ts...)>;

    emp_assert(dynamic_cast<fun_t *>(this));  // Make sure this Call cast is legal.

    fun_t * fun = (fun_t *) this;
    return fun->Call( std::forward<Ts>(args)... );
  }

  template <typename RETURN, typename... Ts>
  bool BaseFunction::CallOK(Ts &&...) {
    using fun_t = DerivedFunction<RETURN(Ts...)>;
    return dynamic_cast<fun_t *>(this);    
  }

  template <typename RETURN, typename... Ts>
  bool BaseFunction::CallTypeOK() {
    using fun_t = DerivedFunction<RETURN(Ts...)>;
    return dynamic_cast<fun_t *>(this);    
  }

  template <typename T> auto BaseFunction::Convert() {
    emp_assert(dynamic_cast<DerivedFunction<T> *>(this));
    return (DerivedFunction<T> *) this;
  }

  template <typename T> bool BaseFunction::ConvertOK() {
    return dynamic_cast<DerivedFunction<T> *>(this);
  }

}

#endif
