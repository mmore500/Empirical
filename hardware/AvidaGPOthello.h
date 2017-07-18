//  This file is part of Empirical, https://github.com/devosoft/Empirical
//  Copyright (C) Michigan State University, 2017.
//  Released under the MIT Software license; see doc/LICENSE
//
//
//  This is a hardcoded CPU for Avida.
//
//
//  Developer Notes:
//  * We should clean up how we handle scope; the root scope is zero, so the arg-based
//    scopes are 1-16 (or however many).  Right now we increment the value in various places
//    and should be more consistent.
//  * How should Avida-GP genomes take an action?  Options include sending ALL outputs and
//    picking the maximum field; sending a single output and using its value; having specialized
//    commands...


#ifndef EMP_AVIDA_GP_H
#define EMP_AVIDA_GP_H

#include <fstream>
#include <iostream>
#include <map>

#include "../base/array.h"
#include "../base/vector.h"
#include "../tools/map_utils.h"
#include "../tools/Random.h"
#include "../tools/string_utils.h"

#include "InstLib.h"

namespace emp {

  class AvidaGP {
  public:
    static constexpr size_t CPU_SIZE = 16;   // Num arg values (for regs, stacks, functions, etc)
    static constexpr size_t INST_ARGS = 3;   // Max num args per instruction.
    static constexpr size_t STACK_CAP = 16;  // Max size for stacks.
    static constexpr size_t BOARD_SIZE = 64; // Size of game board
    static constexpr double PLAYER = 1;
    static constexpr double OPPONENT = -1;
    static constexpr double EMPTY = 0;

    using arg_t = size_t;        // All arguments are non-negative ints (indecies!)
    using arg_set_t = emp::array<arg_t, INST_ARGS>;

    struct Instruction {
      size_t id;
      arg_set_t args;

      Instruction(size_t _id=0, size_t a0=0, size_t a1=0, size_t a2=0)
	      : id(_id), args() { args[0] = a0; args[1] = a1; args[2] = a2; }
      Instruction(const Instruction &) = default;
      Instruction(Instruction &&) = default;

      Instruction & operator=(const Instruction &) = default;
      Instruction & operator=(Instruction &&) = default;

      void Set(size_t _id, size_t _a0=0, size_t _a1=0, size_t _a2=0)
	      { id = _id; args[0] = _a0; args[1] = _a1; args[2] = _a2; }

    };

    struct ScopeInfo {
      size_t scope;
      ScopeType type;
      size_t start_pos;

      ScopeInfo() : scope(0), type(ScopeType::BASIC), start_pos(0) { ; }
      ScopeInfo(size_t _s, ScopeType _t, size_t _p) : scope(_s), type(_t), start_pos(_p) { ; }
    };

    struct RegBackup {
      size_t scope;
      size_t reg_id;
      double value;

      RegBackup() : scope(0), reg_id(0), value(0.0) { ; }
      RegBackup(size_t _s, size_t _r, double _v) : scope(_s), reg_id(_r), value(_v) { ; }
    };

    using inst_t = Instruction;
    using genome_t = emp::vector<inst_t>;
    using inst_lib_t = InstLib<AvidaGP>;
    using stack_t = emp::vector<double>;

  protected:
    Ptr<inst_lib_t> inst_lib;

    // Virtual CPU Components!
    genome_t genome;
    emp::array<double, CPU_SIZE> regs;
    std::unordered_map<int, double> inputs;   // Map of all available inputs (position -> value)
    std::unordered_map<int, double> outputs;  // Map of all outputs (position -> value)
    emp::array< std::unordered_map<int, double>, CPU_SIZE > mem;
    emp::array< int, CPU_SIZE> fun_starts;

    size_t inst_ptr;
    emp::vector<ScopeInfo> scope_stack;
    emp::vector<RegBackup> reg_stack;
    emp::vector<size_t> call_stack;
    emp::vector<double> board;


    size_t errors;

    // A simple way of recording which traits a CPU has demonstrated, and at what qaulity.
    emp::vector<double> traits;

    // Run every time we need to exit the current scope.
    void ExitScope() {
      emp_assert(scope_stack.size() > 1, CurScope());
      emp_assert(scope_stack.size() <= CPU_SIZE, CurScope());

      // Restore any backed-up registers from this scope...
      while (reg_stack.size() && reg_stack.back().scope == CurScope()) {
        regs[reg_stack.back().reg_id] = reg_stack.back().value;
        reg_stack.pop_back();
      }

      // Remove the inner-most scope.
      scope_stack.pop_back();
    }

    // This function is run every time scope changed (if, while, scope instructions, etc.)
    // If we are moving to an outer scope (lower value) we need to close the scope we are in,
    // potentially continuing with a loop.
    bool UpdateScope(size_t new_scope, ScopeType type=ScopeType::BASIC) {
      const size_t cur_scope = CurScope();
      new_scope++;                           // Scopes are stored as one higher than regs (Outer is 0)
      // Test if we are entering a deeper scope.
      if (new_scope > cur_scope) {
        scope_stack.emplace_back(new_scope, type, inst_ptr);
        return true;
      }

      // Otherwise we are potentially exiting the current scope.  Loop back instead?
      if (CurScopeType() == ScopeType::LOOP) {
        inst_ptr = scope_stack.back().start_pos;  // Move back to the beginning of the loop.
        ExitScope();                              // Clear former scope
        ProcessInst( genome[inst_ptr] );          // Process loops start again.
        return false;                             // We did NOT enter the new scope.
      }

      // Or are we exiting a function?
      if (CurScopeType() == ScopeType::FUNCTION) {
        // @CAO Make sure we exit multiple scopes if needed to close the function...
        inst_ptr = call_stack.back();             // Return from the function call.
        if (inst_ptr >= genome.size()) ResetIP(); // Call may have occured at end of genome.
        else {
          call_stack.pop_back();                  // Clear the return position from the call stack.
          ExitScope();                            // Leave the function scope.
        }
        ProcessInst( genome[inst_ptr] );          // Process the new instruction instead.
        return false;                             // We did NOT enter the new scope.
      }

      // If we made it here, we must simply exit the current scope and test again.
      ExitScope();

      return UpdateScope(new_scope, type);
    }

    // This function fast-forwards to the end of the specified scope.
    // NOTE: Bypass scope always drops out of the innermost scope no matter the arg provided.
    void BypassScope(size_t scope) {
      scope++;                           // Scopes are stored as one higher than regs (Outer is 0)
      if (CurScope() < scope) return;    // Only continue if break is relevant for current scope.

      ExitScope();
      while (inst_ptr+1 < genome.size()) {
        inst_ptr++;
        const size_t test_scope = InstScope(genome[inst_ptr]);

        // If this instruction sets the scope AND it's outside the one we want to end, stop here!
        if (test_scope && test_scope <= scope) {
          inst_ptr--;
          break;
        }
      }
    }

  public:
    AvidaGP(Ptr<inst_lib_t> _ilib)
      : inst_lib(_ilib), genome(), regs(), inputs(), outputs(), mem(), fun_starts()
      , inst_ptr(0), scope_stack(), reg_stack(), call_stack(), errors(0), traits()
    {
      scope_stack.emplace_back(0, ScopeType::ROOT, 0);  // Initial scope.
      Reset();
    }
    AvidaGP(inst_lib_t & _ilib) : AvidaGP(&_ilib) { ; }
    AvidaGP() : AvidaGP(DefaultInstLib()) { ; }
    AvidaGP(const AvidaGP &) = default;
    AvidaGP(AvidaGP &&) = default;
    ~AvidaGP() { ; }

    /// Reset the entire CPU to a starting state, without a genome.
    void Reset() {
      genome.resize(0);  // Clear out genome
      traits.resize(0);  // Clear out traits
      SetTrait(100, 0);
      ResetHardware();   // Reset the full hardware
    }

    /// Reset just the CPU hardware, but keep the genome and traits.
    void ResetHardware() {
      // Initialize registers to their posision.  So Reg0 = 0 and Reg11 = 11.
      for (size_t i = 0; i < CPU_SIZE; i++) {
        regs[i] = (double) i;
        inputs.clear();
        outputs.clear();
        mem[i].clear();
        fun_starts[i] = -1;
      }
      inst_ptr = 0;
      scope_stack.resize(1);
      reg_stack.resize(0);
      call_stack.resize(0);
      board.resize(BOARD_SIZE, 0);
      errors = 0;
    }

    /// Reset the instruction pointer to the beginning of the genome AND reset scope.
    void ResetIP() {
      inst_ptr = 0;
      while (scope_stack.size() > 1) ExitScope();  // Forcibly exit all scopes except root.
      // Restore all remaining backed-up registers (likely backed up in outer-most scope).
      while (reg_stack.size()) {
        regs[reg_stack.back().reg_id] = reg_stack.back().value;
        reg_stack.pop_back();
      }
      call_stack.resize(0);
    }

    // Accessors
    Ptr<inst_lib_t> GetInstLib() const { return inst_lib; }
    inst_t GetInst(size_t pos) const { return genome[pos]; }
    const genome_t & GetGenome() const { return genome; }
    double GetReg(size_t id) const { return regs[id]; }
    double GetInput(int id) const { return Find(inputs, id, 0.0); }
    const std::unordered_map<int,double> & GetInputs() const { return inputs; }
    size_t GetNumInputs() const { return inputs.size(); }
    double GetOutput(int id) const { return Find(outputs, id, 0.0); }
    const std::unordered_map<int,double> & GetOutputs() const { return outputs; }
    size_t GetNumOutputs() const { return outputs.size(); }
    int GetFunStart(size_t id) const { return fun_starts[id]; }
    size_t GetIP() const { return inst_ptr; }
    emp::vector<ScopeInfo> GetScopeStack() const { return scope_stack; }
    size_t CurScope() const { return scope_stack.back().scope; }
    ScopeType CurScopeType() const { return scope_stack.back().type; }
    ScopeType GetScopeType(size_t id) { return inst_lib->GetScopeType(id); }
    emp::vector<RegBackup> GetRegStack() const { return reg_stack; }
    emp::vector<size_t> GetCallStack() const { return call_stack; }
    size_t GetNumErrors() const { return errors; }
    double GetTrait(size_t id) const { return traits[id]; }
    const emp::vector<double> &  GetTraits() { return traits; }
    size_t GetNumTraits() const { return traits.size(); }
    double GetMem(size_t block, int posReg) {
      if (mem[block].size() == 0) return 0.0;
      bool full = emp::Has(mem[block], posReg);
      if (!full) return 0.0;
      return mem[block][posReg];
    }

    void SetBoard() { for (int i = 0; i < BOARD_SIZE; i ++){ board[i] = inputs[i]; }}
    double GetSquareCurr(size_t pos) { return board[pos % BOARD_SIZE]; }

    double GetValidAbove(size_t pos) {
      if (pos > 63) {return 0;}
      //std::cout<<"Board PosA: "<<pos<<" "<<board[pos]<<std::endl;
      double square = int(pos) - 8;
      if (board[pos] != EMPTY) {return 0;}
      while (square >= 0){
        //std::cout<<"H: "<<square<< " "<<pos<<std::endl;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square + 8] == OPPONENT) {return 1;}
          else {return 0;}
        }
        square -= 8;
      } 
      return 0;
    }
    double GetValidBelow(size_t pos) {
      if (pos > 63) {return 0;}
      double square = pos + 8;
      if (board[pos] != EMPTY) {return 0;}
      while (square < 64){
        //std::cout<<"G: "<<square<<std::endl;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square - 8] == OPPONENT) {return 1;}
          else {return 0;}
        }
        square += 8;
      } 
      return 0;
    }
    double GetValidLeft(size_t pos) {
      if (pos > 63) {return 0;}
      int square = int(pos);
      if (board[pos] != EMPTY) {return 0;}
      while (square % 8 != 0){
        //std::cout<<"F: "<<square<<std::endl;
        square--;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square + 1] == OPPONENT) {return 1;}
          else {return 0;}
        }
      } 
      return 0;
    }
    double GetValidRight(size_t pos) {
      if (pos > 63) {return 0;}
      int square = int(pos);
      //std::cout<<"Board Pos: "<<board[pos]<<std::endl;
      if (board[pos] != EMPTY) {return 0;}
      while (square % 8 != 7){
        //std::cout<<"E: "<<square<<std::endl;
        square++;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square - 1] == OPPONENT) {return 1;}
          else {return 0;}
        }
      } 
      return 0;
    }
    double GetValidUL(size_t pos) {
      if (pos > 63) {return 0;}
      int square = int(pos);
      if (board[pos] != EMPTY) {return 0;}
      while (square % 8 != 0 && square > 7){
        //std::cout<<"A: "<<square<<std::endl;
        square--;
        square -= 8;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square + 1 + 8] == OPPONENT) {return 1;}
          else {return 0;}
        }
      } 
      return 0;
    }
    double GetValidUR(size_t pos) {
      if (pos > 63) {return 0;}
      int square = int(pos);
      if (board[pos] != EMPTY) {return 0;}
      while (square % 8 != 7 && square > 7){
        //std::cout<<"B: "<<square<<std::endl;
        square++;
        square -= 8;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square - 1 + 8] == OPPONENT) {return 1;}
          else {return 0;}
        }
      } 
      return 0;
    }
    double GetValidLL(size_t pos) {
      if (pos > 63) {return 0;}
      int square = int(pos);
      //std::cout<<"Board PosLL: "<<board[pos]<<std::endl;
      if (board[pos] != EMPTY) {return 0;}
      while (square % 8 != 0 && square < 56){
        //std::cout<<"C: "<<square<<std::endl;
        square--;
        square += 8;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square + 1 - 8] == OPPONENT) {return 1;}
          else {return 0;}
        }
      } 
      return 0;
    }
    double GetValidLR(size_t pos) {
      if (pos > 63) {return 0;}
      int square = int(pos);
      if (board[pos] != EMPTY) {return 0;}
      while (square % 8 != 7 && square < 56){
        //std::cout<<"D: "<<square<<std::endl;
        square++;
        square += 8;
        if (board[square] == EMPTY) {return 0;}
        else if (board[square] == PLAYER) {
          if (board[square - 1 - 8] == OPPONENT) {return 1;}
          else {return 0;}
        }
      } 
      return 0;
    }

    void SetInst(size_t pos, const inst_t & inst) { genome[pos] = inst; }
    void SetInst(size_t pos, size_t id, size_t a0=0, size_t a1=0, size_t a2=0) {
      genome[pos].Set(id, a0, a1, a2);
    }
    void SetGenome(const genome_t & g) { genome = g; }
    void SetReg(size_t id, double val) { regs[id] = val; }
    void SetInput(int input_id, double value) { inputs[input_id] = value; }
    void SetInputs(const std::unordered_map<int,double> & vals) { inputs = vals; }
    void SetInputs(std::unordered_map<int,double> && vals) { inputs = std::move(vals); }
    void SetOutput(int output_id, double value) { outputs[output_id] = value; }
    void SetOutputs(const std::unordered_map<int,double> & vals) { outputs = vals; }
    void SetOutputs(std::unordered_map<int,double> && vals) { outputs = std::move(vals); }
    void SetMem(size_t block, double posReg, double value) { mem[block][int(posReg)] = value; }
    void CopyMem(size_t block_from, size_t block_to) { mem[block_to] = mem[block_from]; }
    void ShiftMem(size_t block, size_t shift_amount) { 
      std::unordered_map<int, double> new_map;
      for (const auto & el : mem[block]) {
        new_map[el.first + shift_amount] = el.second;
        //std::cout<<"Start Location: " << el.first<< " Shift: " << shift_amount << " New Location: " << el.first + shift_amount << std::endl;
      }
      mem[block] = new_map; 
    }
    /*double PopStack(size_t id) {
      if (stacks[id].size() == 0) return 0.0;
      double out = stacks[id].back();
      stacks[id].pop_back();
      return out;
    }
    void PushStack(size_t id, double value) {
      if (stacks[id].size() >= STACK_CAP) return;
      stacks[id].push_back(value);
    }*/
    void SetFunStart(size_t id, int value) { fun_starts[id] = value; }
    void SetIP(size_t pos) { inst_ptr = pos; }
    void PushRegInfo(size_t scope_id, size_t reg_id) {
      reg_stack.emplace_back(scope_id, reg_id, regs[reg_id]);
    }
    void PushCallInfo(size_t pos) { call_stack.push_back(pos); }
    void IncErrors() { errors++; }
    void SetTrait(size_t id, double val) {
      if (id >= traits.size()) traits.resize(id+1, 0.0);
      traits[id] = val;
    }
    void PushTrait(double val) { traits.push_back(val); }
    void EndTurn() { SetTrait(100, 1); }
    inst_t GetRandomInst(Random & rand) {
      return inst_t(rand.GetUInt(inst_lib->GetSize()),
                    rand.GetUInt(CPU_SIZE), rand.GetUInt(CPU_SIZE), rand.GetUInt(CPU_SIZE));
    }

    void RandomizeInst(size_t pos, Random & rand) { SetInst(pos, GetRandomInst(rand) ); }

    void PushInst(size_t id, size_t a0=0, size_t a1=0, size_t a2=0) {
      genome.emplace_back(id, a0, a1, a2);
    }
    void PushInst(const std::string & name, size_t a0=0, size_t a1=0, size_t a2=0) {
      size_t id = inst_lib->GetID(name);
      genome.emplace_back(id, a0, a1, a2);
    }
    void PushInst(const Instruction & inst) { genome.emplace_back(inst); }
    void PushInst(Instruction && inst) { genome.emplace_back(inst); }
    void PushRandom(Random & rand, const size_t count=1) {
      for (size_t i = 0; i < count; i++) {
        PushInst(GetRandomInst(rand));
      }
    }

    // Loading whole genomes.
    bool Load(std::istream & input);

    /// Process a specified instruction, provided by the caller.
    void ProcessInst(const inst_t & inst) { inst_lib->ProcessInst(*this, inst); }

    /// Determine the scope associated with a particular instruction.
    size_t InstScope(const inst_t & inst) const;

    /// Process the NEXT instruction pointed to be the instruction pointer
    void SingleProcess() {
      if (inst_ptr >= genome.size()) ResetIP();
      inst_lib->ProcessInst(*this, genome[inst_ptr]);
      inst_ptr++;
    }

    /// Process the next SERIES of instructions, directed by the instruction pointer.
    void Process(size_t num_inst) { 
      for (size_t i = 0; i < num_inst; i++) {
        if (GetTrait(100) == 1) break;
        SingleProcess(); 
      }
    }

    /// Print out a single instruction, with its arguments.
    void PrintInst(const inst_t & inst, std::ostream & os=std::cout) const;

    /// Print out this program.
    void PrintGenome(std::ostream & os=std::cout) const;
    void PrintGenome(const std::string & filename) const;

    /// Figure out which instruction is going to actually be run next SingleProcess()
    size_t PredictNextInst() const;

    /// Print out the state of the virtual CPU.
    void PrintState(std::ostream & os=std::cout) const;

    /// Trace the instructions being exectured, with full CPU details.
    void Trace(size_t num_inst, std::ostream & os=std::cout) {
      for (size_t i = 0; i < num_inst; i++) { PrintState(os); SingleProcess(); }
    }
    void Trace(size_t num_inst, const std::string & filename) {
      std::ofstream of(filename);
      Trace(num_inst, of);
      of.close();
    }


    /// Instructions
    static void Inst_Inc(AvidaGP & hw, const arg_set_t & args) { ++hw.regs[args[0]]; }
    static void Inst_Dec(AvidaGP & hw, const arg_set_t & args) { --hw.regs[args[0]]; }
    static void Inst_Not(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[0]] = (hw.regs[args[0]] == 0.0); }
    static void Inst_SetReg(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[0]] = (double) args[1]; }
    static void Inst_Add(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[2]] = hw.regs[args[0]] + hw.regs[args[1]]; }
    static void Inst_Sub(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[2]] = hw.regs[args[0]] - hw.regs[args[1]]; }
    static void Inst_Mult(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[2]] = hw.regs[args[0]] * hw.regs[args[1]]; }

    static void Inst_Div(AvidaGP & hw, const arg_set_t & args) {
      const double denom = hw.regs[args[1]];
      if (denom == 0.0) ++hw.errors;
      else hw.regs[args[2]] = hw.regs[args[0]] / denom;
    }

    static void Inst_Mod(AvidaGP & hw, const arg_set_t & args) {
      const double base = hw.regs[args[1]];
      if (base == 0.0) ++hw.errors;
      else hw.regs[args[2]] = hw.regs[args[0]] / base;
    }

    static void Inst_TestEqu(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[2]] = (hw.regs[args[0]] == hw.regs[args[1]]); }
    static void Inst_TestNEqu(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[2]] = (hw.regs[args[0]] != hw.regs[args[1]]); }
    static void Inst_TestLess(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[2]] = (hw.regs[args[0]] < hw.regs[args[1]]); }

    static void Inst_If(AvidaGP & hw, const arg_set_t & args) { // args[0] = test, args[1] = scope
      if (hw.UpdateScope(args[1]) == false) return;      // If previous scope is unfinished, stop!
      if (hw.regs[args[0]] == 0.0) hw.BypassScope(args[1]); // If test fails, move to scope end.
    }

    static void Inst_While(AvidaGP & hw, const arg_set_t & args) {
      // UpdateScope returns false if previous scope isn't finished (e.g., while needs to loop)
      if (hw.UpdateScope(args[1], ScopeType::LOOP) == false) return;
      if (hw.regs[args[0]] == 0.0) hw.BypassScope(args[1]); // If test fails, move to scope end.
    }

    static void Inst_Countdown(AvidaGP & hw, const arg_set_t & args) {  // Same as while, but auto-decriments test each loop.
      // UpdateScope returns false if previous scope isn't finished (e.g., while needs to loop)
      if (hw.UpdateScope(args[1], ScopeType::LOOP) == false) return;
      if (hw.regs[args[0]] == 0.0) hw.BypassScope(args[1]);   // If test fails, move to scope end.
      else hw.regs[args[0]]--;
    }

    static void Inst_Break(AvidaGP & hw, const arg_set_t & args) { hw.BypassScope(args[0]); }
    static void Inst_Scope(AvidaGP & hw, const arg_set_t & args) { hw.UpdateScope(args[0]); }

    static void Inst_Define(AvidaGP & hw, const arg_set_t & args) {
      if (hw.UpdateScope(args[1]) == false) return; // Update which scope we are in.
      hw.fun_starts[args[0]] = (int) hw.inst_ptr;     // Record where function should be exectuted.
      hw.BypassScope(args[1]);                     // Skip over the function definition for now.
    }

    static void Inst_Call(AvidaGP & hw, const arg_set_t & args) {
      // Make sure function exists and is still in place.
      size_t def_pos = (size_t) hw.fun_starts[args[0]];
      if (def_pos >= hw.genome.size()
          || hw.GetScopeType(hw.genome[def_pos].id) != ScopeType::FUNCTION) return;

      // Go back into the function's original scope (call is in that scope)
      size_t fun_scope = hw.genome[def_pos].args[1];
      if (hw.UpdateScope(fun_scope, ScopeType::FUNCTION) == false) return;
      hw.call_stack.push_back(hw.inst_ptr+1);        // Back up the call position
      hw.inst_ptr = def_pos+1;                       // Jump to the function body (will adavance)
    }

    //static void Inst_Push(AvidaGP & hw, const arg_set_t & args) { hw.PushStack(args[1], hw.regs[args[0]]); }
    //static void Inst_Pop(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[1]] = hw.PopStack(args[0]); }

    static void Inst_Input(AvidaGP & hw, const arg_set_t & args) {
      // Determine the input ID and grab it if it exists; if not, return 0.0
      int input_id = (int) hw.regs[ args[0] ];
      hw.regs[args[1]] = Find(hw.inputs, input_id, 0.0);
    }

    static void Inst_Output(AvidaGP & hw, const arg_set_t & args) {
      // Save the date in the target reg to the specified output position.
      int output_id = (int) hw.regs[ args[1] ];  // Grab ID from register.
      hw.outputs[output_id] = hw.regs[args[0]];     // Copy target reg to appropriate output.
      //std::cout<<"Output: "<<hw.regs[args[0]]<<std::endl;
    }

    static void Inst_CopyVal(AvidaGP & hw, const arg_set_t & args) { hw.regs[args[1]] = hw.regs[args[0]]; }

    static void Inst_ScopeReg(AvidaGP & hw, const arg_set_t & args) {
      hw.reg_stack.emplace_back(hw.CurScope(), args[0], hw.regs[args[0]]);
    }

    static void Inst_SetBoard(AvidaGP & hw, const arg_set_t & args) {
      hw.SetBoard();
    }
    static void Inst_GetSquareCurr(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetSquareCurr(int(hw.regs[args[0]]));
      hw.regs[args[1]] = out;
    }
    static void Inst_EndTurn(AvidaGP & hw, const arg_set_t & args) {
      hw.EndTurn();
    }

    static void Inst_GetValidAbove(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidAbove(int(hw.regs[args[0]]));
      //std::cout << "A: "<<out<<std::endl;
      hw.regs[args[1]] = out;
    }

    static void Inst_GetValidBelow(AvidaGP & hw, const arg_set_t & args) {
      //std::cout<<"HERE"<<std::endl;
      double out = hw.GetValidBelow(int(hw.regs[args[0]]));
      hw.regs[args[1]] = out;
      //std::cout << "B: "<<out<<std::endl;
    }
    static void Inst_GetValidLeft(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidLeft(int(hw.regs[args[0]]));
      hw.regs[args[1]] = out;
      //std::cout << "L: "<<out<<std::endl;
    }
    static void Inst_GetValidRight(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidRight(int(hw.regs[args[0]]));
      hw.regs[args[1]] = out;
      //std::cout << "R: "<<out<<std::endl;
    }
    static void Inst_GetValidUL(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidUL(int(hw.regs[args[0]]));
      hw.regs[args[1]] = out;
      //std::cout << "UL: "<<out<<std::endl;
    }
    static void Inst_GetValidUR(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidUR(int(hw.regs[args[0]]));
      hw.regs[args[1]] = out;
      //std::cout << "UR: "<<out<<std::endl;
    }
    static void Inst_GetValidLL(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidLL(int(hw.regs[args[0]]));
      //std::cout << "LL: "<<out<<std::endl;
      hw.regs[args[1]] = out;
    }
    static void Inst_GetValidLR(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetValidLR(int(hw.regs[args[0]]));
      //std::cout << "LR: "<<out<<std::endl;
      hw.regs[args[1]] = out;
    }

    static void Inst_GetMem(AvidaGP & hw, const arg_set_t & args) {
      double out = hw.GetMem(args[0], int(hw.regs[args[1]]));
      hw.regs[args[2]] = out;
      //std::cout<< "######### GETT Block: " << block << " Pos: " << int(posReg) << " Value: " << int(out) << std::endl;
    }

    static void Inst_SetMem(AvidaGP & hw, const arg_set_t & args) {
      hw.SetMem(args[0], hw.regs[args[1]], hw.regs[args[2]]);
      //std::cout<< "SET Block: " << block << " Pos: " << int(posReg) << " Value: " << int(value) << std::endl;
    }

    static void Inst_CopyMem(AvidaGP & hw, const arg_set_t & args) {
      hw.CopyMem(args[0], args[1]);
      //std::cout<< "COPY Block To: " << block_to << " Block From: " << block_from << std::endl;
    }

    static void Inst_ShiftMem(AvidaGP & hw, const arg_set_t & args) {
      hw.ShiftMem(args[0], hw.regs[args[1]]);
    }

    static Ptr<inst_lib_t> DefaultInstLib();
  };

  size_t AvidaGP::InstScope(const inst_t & inst) const {
    if (inst_lib->GetScopeType(inst.id) == ScopeType::NONE) return 0;
    return inst.args[ inst_lib->GetScopeArg(inst.id) ] + 1;
  }

  void AvidaGP::PrintInst(const inst_t & inst, std::ostream & os) const {
    os << inst_lib->GetName(inst.id);
    const size_t num_args = inst_lib->GetNumArgs(inst.id);
    for (size_t i = 0; i < num_args; i++) {
      os << ' ' << inst.args[i];
    }
  }

  void AvidaGP::PrintGenome(std::ostream & os) const {
    size_t cur_scope = 0;

    for (const inst_t & inst : genome) {
      size_t new_scope = InstScope(inst);

      if (new_scope) {
        if (new_scope == cur_scope) {
          for (size_t i = 0; i < cur_scope; i++) os << ' ';
          os << "----\n";
        }
        if (new_scope < cur_scope) {
          cur_scope = new_scope-1;
        }
      }

      for (size_t i = 0; i < cur_scope; i++) os << ' ';
      PrintInst(inst, os);
      if (new_scope) {
        if (new_scope > cur_scope) os << " --> ";
        cur_scope = new_scope;
      }
      os << '\n';
    }
  }

  void AvidaGP::PrintGenome(const std::string & filename) const {
    std::ofstream of(filename);
    PrintGenome(of);
    of.close();
  }

  size_t AvidaGP::PredictNextInst() const {
    // Determine if we are changing scope.
    size_t new_scope = CPU_SIZE+1;  // Default to invalid scope.
    if (inst_ptr >= genome.size()) new_scope = 0;
    else {
      size_t inst_scope = InstScope(genome[inst_ptr]);
      if (inst_scope) new_scope = inst_scope;
    }

    // If we are not changing scope OR we are going to a deeper scope, execute next!
    if (new_scope > CPU_SIZE || new_scope > CurScope()) return inst_ptr;

    // If we are at the end of a loop, assume we will jump back to the beginning.
    if (CurScopeType() == ScopeType::LOOP) {
      return scope_stack.back().start_pos;
    }

    // If we are at the end of a function, assume we will jump back to the call.
    if (CurScopeType() == ScopeType::FUNCTION) {
      size_t next_pos = call_stack.back();
      if (next_pos >= genome.size()) next_pos = 0;
      return next_pos;
    }

    // If we have run past the end of the genome, we will start over.
    if (inst_ptr >= genome.size()) return 0;

    // Otherwise, we exit the scope normally.
    return inst_ptr;
  }

  void AvidaGP::PrintState(std::ostream & os) const {
    size_t next_inst = PredictNextInst();

    os << " REGS: ";
    for (size_t i = 0; i < CPU_SIZE; i++) os << "[" << regs[i] << "] ";
    os << "\n INPUTS: ";
    // for (size_t i = 0; i < CPU_SIZE; i++) os << "[" << Find(inputs, (int)i, 0.0) << "] ";
    for (auto & x : inputs) os << "[" << x.first << "," << x.second << "] ";
    os << "\n OUTPUTS: ";
    //for (size_t i = 0; i < CPU_SIZE; i++) os << "[" << Find(outputs, (int)i, 0.0) << "] ";
    for (auto & x : outputs) os << "[" << x.first << "," << x.second << "] ";
    os << std::endl;

    os << "IP:" << inst_ptr;
    if (inst_ptr != next_inst) os << "(-> " << next_inst << ")";
    os << " scope:" << CurScope()
       << " (";
    PrintInst(genome[next_inst], os);
    os << ")"
       << " errors: " << errors
       << std::endl;

    // @CAO Still need:
    // emp::array< emp::vector<double>, CPU_SIZE > stacks;
    // emp::array< int, CPU_SIZE> fun_starts;
    // emp::vector<RegBackup> reg_stack;
    // emp::vector<size_t> call_stack;
  }

  /// This static function can be used to access the generic AvidaGP instruction library.
  Ptr<InstLib<AvidaGP>> AvidaGP::DefaultInstLib() {
    static inst_lib_t inst_lib;

    if (inst_lib.GetSize() == 0) {
      inst_lib.AddInst("Inc", Inst_Inc, 1, "Increment value in reg Arg1");
      inst_lib.AddInst("Dec", Inst_Dec, 1, "Decrement value in reg Arg1");
      inst_lib.AddInst("Not", Inst_Not, 1, "Logically toggle value in reg Arg1");
      inst_lib.AddInst("SetReg", Inst_SetReg, 2, "Set reg Arg1 to numerical value Arg2");
      inst_lib.AddInst("Add", Inst_Add, 3, "regs: Arg3 = Arg1 + Arg2");
      inst_lib.AddInst("Sub", Inst_Sub, 3, "regs: Arg3 = Arg1 - Arg2");
      inst_lib.AddInst("Mult", Inst_Mult, 3, "regs: Arg3 = Arg1 * Arg2");
      inst_lib.AddInst("Div", Inst_Div, 3, "regs: Arg3 = Arg1 / Arg2");
      inst_lib.AddInst("Mod", Inst_Mod, 3, "regs: Arg3 = Arg1 % Arg2");
      inst_lib.AddInst("TestEqu", Inst_TestEqu, 3, "regs: Arg3 = (Arg1 == Arg2)");
      inst_lib.AddInst("TestNEqu", Inst_TestNEqu, 3, "regs: Arg3 = (Arg1 != Arg2)");
      inst_lib.AddInst("TestLess", Inst_TestLess, 3, "regs: Arg3 = (Arg1 < Arg2)");
      inst_lib.AddInst("If", Inst_If, 2, "If reg Arg1 != 0, scope -> Arg2; else skip scope", ScopeType::BASIC, 1);
      inst_lib.AddInst("While", Inst_While, 2, "Until reg Arg1 != 0, repeat scope Arg2; else skip", ScopeType::LOOP, 1);
      inst_lib.AddInst("Countdown", Inst_Countdown, 2, "Countdown reg Arg1 to zero; scope to Arg2", ScopeType::LOOP, 1);
      inst_lib.AddInst("Break", Inst_Break, 1, "Break out of scope Arg1");
      inst_lib.AddInst("Scope", Inst_Scope, 1, "Enter scope Arg1", ScopeType::BASIC, 0);
      inst_lib.AddInst("Define", Inst_Define, 2, "Build function Arg1 in scope Arg2", ScopeType::FUNCTION, 1);
      inst_lib.AddInst("Call", Inst_Call, 1, "Call previously defined function Arg1");
      inst_lib.AddInst("SetMem", Inst_SetMem, 3, "Put reg Arg3 into mem block Arg1 at position reg Arg2");
      inst_lib.AddInst("GetMem", Inst_GetMem, 3, "Get from block Arg1 position reg Arg2 into reg Arg3");
      inst_lib.AddInst("CopyMem", Inst_CopyMem, 2, "Copy memory block Arg1 into memory block Arg2");
      inst_lib.AddInst("ShiftMem", Inst_ShiftMem, 2, "Shift memory block Arg1 into memory block Arg2");
      inst_lib.AddInst("SetBoard", Inst_SetBoard, 1, "Sets board state in board memory");
      inst_lib.AddInst("EndTurn", Inst_EndTurn, 1, "Signals that the organism is done with computation");
      inst_lib.AddInst("GetSquareCurr", Inst_GetSquareCurr, 2, "Gets piece from reg Arg 1 in board and puts it in reg Arg 2");
      inst_lib.AddInst("GetValidAbove", Inst_GetValidAbove, 2, "Check if reg Arg1 flanks a piece above, bool put in reg Arg2");
      inst_lib.AddInst("GetValidBelow", Inst_GetValidBelow, 2, "Check if reg Arg1 flanks a piece below, bool put in reg Arg2");
      inst_lib.AddInst("GetValidLeft", Inst_GetValidLeft, 2, "Check if reg Arg1 flanks a piece left, bool put in reg Arg2");
      inst_lib.AddInst("GetValidRight", Inst_GetValidRight, 2, "Check if reg Arg1 flanks a piece right, bool put in reg Arg2");
      inst_lib.AddInst("GetValidUL", Inst_GetValidUL, 2, "Check if reg Arg1 flanks a piece upper left, bool put in reg Arg2");
      inst_lib.AddInst("GetValidUR", Inst_GetValidUR, 2, "Check if reg Arg1 flanks a piece upper right, bool put in reg Arg2");
      inst_lib.AddInst("GetValidLL", Inst_GetValidLL, 2, "Check if reg Arg1 flanks a piece lower left, bool put in reg Arg2");
      inst_lib.AddInst("GetValidLR", Inst_GetValidLR, 2, "Check if reg Arg1 flanks a piece lower right, bool put in reg Arg2");
      //inst_lib.AddInst("Push", Inst_Push, 2, "Push reg Arg1 onto stack Arg2");
      //inst_lib.AddInst("Pop", Inst_Pop, 2, "Pop stack Arg1 into reg Arg2");
      inst_lib.AddInst("Input", Inst_Input, 2, "Pull next value from input Arg1 into reg Arg2");
      inst_lib.AddInst("Output", Inst_Output, 2, "Push reg Arg1 into output Arg2");
      inst_lib.AddInst("CopyVal", Inst_CopyVal, 2, "Copy reg Arg1 into reg Arg2");
      inst_lib.AddInst("ScopeReg", Inst_ScopeReg, 1, "Backup reg Arg1; restore at end of scope");

      for (size_t i = 0; i < CPU_SIZE; i++) {
        inst_lib.AddArg(to_string((int)i), i);                   // Args can be called by value
        inst_lib.AddArg(to_string("Reg", 'A'+(char)i), i);  // ...or as a register.
      }
    }

    return &inst_lib;
  }

}


#endif