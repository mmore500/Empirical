/**
 *  @note This file is part of Empirical, https://github.com/devosoft/Empirical
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2017-2018
 *
 *  @file  World_structure.h
 *  @brief Functions for popular world structure methods.
 */

#ifndef EMP_EVO_WORLD_STRUCTURE_H
#define EMP_EVO_WORLD_STRUCTURE_H

#include "../base/assert.h"
#include "../base/vector.h"
#include "../data/Trait.h"
#include "../tools/Random.h"
#include "../tools/vector_utils.h"

namespace emp {

  template <typename ORG> class World;

  /// A class to track positions in World.
  /// For the moment, the only informaiton beyond index is active (vs. next) population when
  /// using synchronous generations.
  //
  //  Developer NOTE: For efficiency, internal class members are uint32_t, but to prevent compiler
  //                  warnings, size_t values are accepted; asserts ensure safe conversions.
  class WorldPosition {
  private:
    uint32_t index;   ///<  Position of this organism in the population.
    uint32_t pop_id;  ///<  ID of the population we are in; 0 is always the active population.

  public:
    static constexpr size_t invalid_id = (uint32_t) -1;

    WorldPosition() : index(invalid_id), pop_id(invalid_id) { ; }
    WorldPosition(size_t _id, size_t _pop_id=0) : index((uint32_t) _id), pop_id((uint32_t) _pop_id) {
      emp_assert(_id <= invalid_id);
      emp_assert(_pop_id <= invalid_id);
    }
    WorldPosition(const WorldPosition &) = default;

    uint32_t GetIndex() const { return index; }
    uint32_t GetPopID() const { return pop_id; }

    bool IsActive() const { return pop_id == 0; }
    bool IsValid() const { return index != invalid_id; }

    WorldPosition & SetActive(bool _active=true) { pop_id = 0; return *this; }
    WorldPosition & SetPopID(size_t _id) { emp_assert(_id <= invalid_id); pop_id = (uint32_t) _id; return *this; }
    WorldPosition & SetIndex(size_t _id) { emp_assert(_id <= invalid_id); index = (uint32_t) _id; return *this; }
    WorldPosition & MarkInvalid() { index = invalid_id; pop_id = invalid_id; return *this; }
  };

  /// Set the population to be a set of pools that are individually well mixed, but with limited
  /// migtation.  Arguments are the number of pools, the size of each pool, and whether the
  /// generations should be synchronous (true) or not (false, default).
  template <typename ORG>
  void SetPools(World<ORG> & world, size_t num_pools,
                size_t pool_size, bool synchronous_gen=false) {
    world.Resize(pool_size, num_pools);
    world.MarkSynchronous(synchronous_gen);
    world.MarkSpaceStructured(true).MarkPhenoStructured(false);

    // -- Setup functions --
    // Inject in an empty pool -or- randomly if none empty
    world.SetAddInjectFun( [&world,pool_size](Ptr<ORG> new_org) {
      for (size_t id = 0; id < world.GetSize(); id += pool_size) {
        if (world.IsOccupied(id) == false) return WorldPosition(id);
      }
      return WorldPosition(world.GetRandomCellID());
    });

    // Neighbors are everyone in the same pool.
    world.SetGetNeighborFun( [&world,pool_size](WorldPosition pos) {
      const size_t pool_start = (pos.GetIndex() / pool_size) * pool_size;
      return pos.SetIndex(pool_start + world.GetRandom().GetUInt(pool_size));
    });

    if (synchronous_gen) {
      // Place births in the next open spot in the new pool (or randomly if full!)
      world.SetAddBirthFun( [&world,pool_size](Ptr<ORG> new_org, WorldPosition parent_pos) {
        emp_assert(new_org);                                  // New organism must exist.
        const size_t parent_id = parent_pos.GetIndex();
        const size_t pool_id = parent_id / pool_size;
        const size_t start_id = pool_id * pool_size;
        for (size_t id = start_id; id < start_id+pool_size; id++) {
          if (world.IsOccupied(WorldPosition(id,1)) == false) {  // Search for an open position...
            return WorldPosition(id, 1);
          }
        }
        WorldPosition pos = world.GetRandomNeighborPos(parent_pos);  // Placed near parent, in next pop.
        return pos.SetPopID(1);
      });
      world.SetAttribute("SynchronousGen", "True");
    } else {
      // Asynchronous: always go to a neighbor in current population.
      world.SetAddBirthFun( [&world](Ptr<ORG> new_org, WorldPosition parent_pos) {
        auto pos = world.GetRandomNeighborPos(parent_pos);
        return pos; // Place org in existing population.
      });
      world.SetAttribute("SynchronousGen", "False");
    }

    world.SetAttribute("PopStruct", "Pools");
  }


  /// Set the population to use a MapElites structure.  This means that organism placement has
  /// two key components:
  /// 1: Organism position is based on their phenotypic traits.
  /// 2: Organisms must have a higher fitness than the current resident of a position to steal it.
  ///
  /// Note: Since organisms compete with their predecessors for space in the populations,
  /// synchronous generations do not make sense.
  ///
  /// This for version will setup a MAP-Elites world; traits to use an how many bins for each
  /// (trait counts) must be provided.
  template <typename ORG>
  void SetMapElites(World<ORG> & world, TraitSet<ORG> traits,
                    const emp::vector<size_t> & trait_counts) {
    world.Resize(trait_counts);  // World sizes are based on counts of traits options.
    world.MarkSynchronous(false);
    world.MarkSpaceStructured(false).MarkPhenoStructured(true);

    // -- Setup functions --
    // Inject into the appropriate positon based on phenotype.  Note that an inject will fail
    // if a more fit organism is already in place; you must run clear first if you want to
    // ensure placement.
    world.SetAddInjectFun( [&world,traits,trait_counts](Ptr<ORG> new_org) {
      // Determine tha position that this phenotype fits in.
      double org_fitness = world.CalcFitnessOrg(*new_org);
      size_t id = traits.EvalBin(*new_org, trait_counts);
      double cur_fitness = world.CalcFitnessID(id);

      if (cur_fitness > org_fitness) return WorldPosition();  // Return invalid position!
      return WorldPosition(id);
    });

    // Map Elites does not have a concept of neighbors.
    world.SetGetNeighborFun( [](WorldPosition pos) { emp_assert(false); return pos; });

    // Birth is effectively the same as inject.
    world.SetAddBirthFun( [&world,traits,trait_counts](Ptr<ORG> new_org, WorldPosition parent_pos) {
      (void) parent_pos; // Parent position is not needed for MAP Elites.
      // Determine tha position that this phenotype fits in.
      double org_fitness = world.CalcFitnessOrg(*new_org);
      size_t id = traits.EvalBin(*new_org, trait_counts);
      double cur_fitness = world.CalcFitnessID(id);

      if (cur_fitness > org_fitness) return WorldPosition();  // Return invalid position!
      return WorldPosition(id);
    });

    world.SetAttribute("SynchronousGen", "False");
    world.SetAttribute("PopStruct", "MapElites");
  }

  /// Setup a MAP-Elites world, given the provided set of traits.
  /// Requires world to already have a size; that size is respected when deciding trait bins.
  template <typename ORG>
  void SetMapElites(World<ORG> & world, TraitSet<ORG> traits) {
    emp::vector<size_t> trait_counts;
    emp_assert(traits.GetSize() > 0);

    // If there's only a single trait, it should get the full population.
    if (traits.GetSize() == 1) {
      trait_counts.push_back(world.GetSize());
      SetMapElites(world, traits, trait_counts);
      return;
    }
    const size_t num_traits = traits.GetSize();
    size_t trait_size = 1;
    while (Pow(trait_size+1, num_traits) < world.GetSize()) trait_size++;
    trait_counts.resize(num_traits, trait_size);
    SetMapElites(world, traits, trait_counts);
  }

  /// Setup a MAP-Elites world, given the provided trait counts (number of bins).
  /// Requires world to already have a phenotypes that those counts are applied to.
  template <typename ORG>
  void SetMapElites(World<ORG> & world, const emp::vector<size_t> & trait_counts) {
    SetMapElites(world, world.GetPhenotypes(), trait_counts);
  }

  /// Setup a MAP-Elites world, given the provided worlds already has size AND set of phenotypes.
  /// Requires world to already have a size; that size is respected when deciding trait bins.
  template <typename ORG>
  void SetMapElites(World<ORG> & world) { SetMapElites(world, world.GetPhenotypes()); }



  /// DiverseElites is similar to MAP-Elites, but rather than merely keep the elites on
  /// a pre-defined grid, it merely tries to maintain maximal distance between elites in
  /// trait space.  The main advantages to this technique are (1) It's easy to build
  /// up an inital population that grows in diversity over time, and (2) You don't need to
  /// predefine box sizes or even limits to trait values.

  /// Set the population to use a DiverseElites structure.  This means that organism placement has
  /// two key components:
  /// 1: Organism position is in continuous space based on phenotypic traits.
  /// 2: When the population is full, nearby organisms must battle to keep their position.
  ///
  /// Note: Since organisms compete with their predecessors for space in the populations,
  /// synchronous generations do not make sense.

  /// Build a class to track distances between organisms.
  // Note: Assuming that once a position is filled it will never be empty again.
  template <typename ORG>
  struct World_MinDistInfo {
    static constexpr size_t ID_NONE = (size_t) -1;                         ///< ID for organism does not exist.
    static constexpr double MAX_DIST = std::numeric_limits<double>::max(); ///< Highest distance for init.

    emp::vector<size_t> nearest_id;                 ///< For each individual, whom are they closest to?
    emp::vector<double> distance;                   ///< And what is their distance?

    World<ORG> & world;
    TraitSet<ORG> traits;
    bool is_setup;

    World_MinDistInfo(World<ORG> & in_world, const TraitSet<ORG> & in_traits)
     : nearest_id(), distance(), world(in_world), traits(in_traits), is_setup(false)
     { ; }

    double CalcDist(size_t id1, size_t id2) {
      emp::vector<double> offsets = traits.CalcOffsets(world.GetOrg(id1), world.GetOrg(id2));
      double dist = 0.0;
      for (double offset : offsets) dist += offset * offset;
      return dist;
    }

    // Find the closest connection to a position again; update neighbors as well!
    void Refresh(size_t refresh_id, size_t start_id = 0) {
      emp_assert(refresh_id < world.GetSize()); // Make sure ID is legal.
      nearest_id[refresh_id] = ID_NONE;
      distance[refresh_id] = MAX_DIST;
      for (size_t id2 = start_id; id2 < world.GetSize(); id2++) {
        if (id2 == refresh_id) continue;
        const double cur_dist = CalcDist(id2, refresh_id);
        if (cur_dist < distance[refresh_id]) {
          distance[refresh_id] = cur_dist;
          nearest_id[refresh_id] = id2;
        }
        if (cur_dist < distance[id2]) {
          distance[id2] = cur_dist;
          nearest_id[id2] = refresh_id;          
        }
      }
    }

    void Setup() {
      emp_assert(world.GetSize() >= 2); // Must have at least 2 orgs in the population to setup.
      nearest_id.resize(world.GetSize());
      distance.resize(world.GetSize());

      for (size_t id = 0; id < world.GetSize(); id++) {
        Refresh(id, id+1);
      }
      is_setup = true;
    }

    void Clear() {
      nearest_id.resize(0);
      distance.resize(0);
      is_setup = false;
    }

    /// Find the best organism to kill in the popualtion.  In this case, find the two closest organisms
    /// and kill the one with the lower fitness.
    size_t FindKill() {
      if (!is_setup) Setup();  // The first time we run out of space and need to kill, setup structure!

      emp_assert(distance.size() > 0);  // After setup, we should always have distances stored.

      const size_t min_id = FindMinIndex(distance);
      if (world.CalcFitnessID(min_id) < world.CalcFitnessID(nearest_id[min_id])) return min_id;
      else return nearest_id[min_id];
    }

    /// Return an empty world position.  If none are available, return the position of an org to be killed.
    size_t GetBirthPos(size_t world_size) {
      // If there's room in the world for one more, get the next empty position.
      if (world.GetSize() < world_size) { return world.GetSize(); }
      // Otherwise, determine whom to kill return their position to be used.
      return FindKill();
    }

    /// Assume a position has changed; refresh it AND everything that had it as a closest connection.
    void Update(size_t pos) {
      if (!is_setup) return;  // Until structure is setup, don't worry about maintaining.
      emp_assert(pos < world.GetSize());
      for (size_t id = 0; id < world.GetSize(); id++) {
        if (nearest_id[id] == pos) Refresh(id);
      }
      Refresh(pos);
    }

    /// A debug function to make sure the internal state is all valid.
    bool OK() {
      // These tests only matter BEFORE Setup() is run.
      if (!is_setup) {
        if (nearest_id.size() != 0) return false;
        if (distance.size() != 0) return false;
      }
 
      // Tests for AFTER Setup() is run.
      else {
        if (nearest_id.size() != world.GetSize()) return false;
        if (distance.size() != world.GetSize()) return false;
      }
      return true;
    }
  };

  /// This first version will setup a Diverse-Elites world and specify traits to use.
  template <typename ORG>
  void SetDiverseElites(World<ORG> & world, TraitSet<ORG> traits, size_t world_size) { 
    world.MarkSynchronous(false);
    world.MarkSpaceStructured(false).MarkPhenoStructured(true);

    // Build a pointer to the current information (and make sure it's deleted later)
    Ptr<World_MinDistInfo<ORG>> info_ptr = NewPtr<World_MinDistInfo<ORG>>(world, traits);
    world.OnWorldDestruct([info_ptr]() mutable { info_ptr.Delete(); });

    // Make sure to update info whenever a new org is placed into the population.
    world.OnPlacement( [info_ptr](size_t pos) mutable { info_ptr->Update(pos); } );

    // -- Setup functions --
    // Inject into the appropriate positon based on phenotype.  Note that an inject will fail
    // if a more fit organism is already in place; you must run clear first if you want to
    // ensure placement.
    world.SetAddInjectFun( [&world, traits, world_size, info_ptr](Ptr<ORG> new_org) {
      size_t pos = info_ptr->GetBirthPos(world_size);
      return WorldPosition(pos);
    });

    // Diverse Elites does not have a concept of neighbors.
    // @CAO Or should we return closest individual, which we already save?
    world.SetGetNeighborFun( [](WorldPosition pos) { emp_assert(false); return pos; });

    // Birth is effectively the same as inject.
    world.SetAddBirthFun( [&world, traits, world_size, info_ptr](Ptr<ORG> new_org, WorldPosition parent_pos) {
      (void) parent_pos;
      size_t pos = info_ptr->GetBirthPos(world_size);
      return WorldPosition(pos);
    });

    world.SetAttribute("SynchronousGen", "False");
    world.SetAttribute("PopStruct", "DiverseElites");
  }

  /// Setup a Diverse-Elites world, given the provided world already has set of phenotypes.
  template <typename ORG>
  void SetDiverseElites(World<ORG> & world, size_t world_size) {
    SetDiverseElites(world, world.GetPhenotypes(), world_size);
  }
}

#endif
