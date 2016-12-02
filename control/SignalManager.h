//  This file is part of Empirical, https://github.com/devosoft/Empirical
//  Copyright (C) Michigan State University, 2016.
//  Released under the MIT Software license; see doc/LICENSE
//
//
//  This file defines the SignalManager class, which collects sets of Signals to be looked up
//  or manipulated later.

#include <string>
#include <unordered_map>

#include "../tools/string_utils.h"

#include "Signal.h"

namespace emp {

  class SignalManager {
  private:
    std::unordered_map<std::string, SignalBase *> signal_map;
    int next_id=0;
    std::string prefix = "emp_signal_";

  public:
    SignalManager() = default;
    SignalManager(SignalManager &&) = default;     // Normal juggle is okay for move constructor
    SignalManager(const SignalManager & in) : next_id(in.next_id), prefix(in.prefix) {
      // Copy all signals from input manager.
      for (const auto & x : in.signal_map) {
        signal_map[x.first] = x.second->Clone();
      }
    }
    ~SignalManager() { for (auto & x : signal_map) delete x.second; }

    int GetNextID() const { return next_id; }
    size_t GetSize() const { return signal_map.size(); }

    SignalBase & Get(const std::string & name) {
      emp_assert(signal_map.find(name) != signal_map.end());
      return *(signal_map[name]);
    }

  };

}
