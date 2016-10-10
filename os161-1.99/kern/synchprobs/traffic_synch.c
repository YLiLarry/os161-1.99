#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */


/*
My Solution:

The condition for a car Va to pass is, for all Vb all followings
are true:

• Va.ori = Vb.ori, or
• Va.ori = Vb.dest and Va.dest = Vb.ori, or
• Va.dest != Vb.dest, and at least one of them is making a right turn

For (1) (2) we use 4 integers representing # of cars from origins, 
and 4 integers represeting the same for dests
For (3) we also a integer representing # of cars NOT making a right turn

*/

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
volatile unsigned int origin[4] = {0};
volatile unsigned int destination[4] = {0};
volatile unsigned int num_not_right = 0;
struct lock* intersection_lock;
struct cv* intersection_cv;

/* helpers */

static bool all_from(Direction o) {
  for (unsigned int i = 0; i <= 3; i++) {
    if (i != o && origin[i] > 0) {
      return false;
    } 
  }
  return true;
}

static bool all_to(Direction d) {
  for (unsigned int i = 0; i <= 3; i++) {
    if (i != d && destination[i] > 0) {
      return false;
    } 
  }
  return true;
}

static bool none_to(Direction d) {
  return destination[d] == 0;
}

static bool is_right_turn(Direction o, Direction d) {
  return (o == north && d == west) || 
         (o == west && d == south) ||  
         (o == south && d == east) ||  
         (o == east && d == north);  
}

static bool car_can_pass(Direction o, Direction d) {
  return all_from(o) ||
         (all_from(d) && all_to(o)) ||
         (none_to(d) && (is_right_turn(o,d) || num_not_right == 0));
}

static void car_passing(Direction o, Direction d) {
  origin[o]++;
  destination[d]++;
  if (! is_right_turn(o,d)) {
    num_not_right++;
  }
}

static void car_passed(Direction o, Direction d) {
  origin[o]--;
  destination[d]--;
  if (! is_right_turn(o,d)) {
    num_not_right--;
  }
}

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  intersection_cv = cv_create("Intersection CV");
  intersection_lock = lock_create("Intersection Lock");
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  cv_destroy(intersection_cv);
  lock_destroy(intersection_lock);  
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  lock_acquire(intersection_lock);
  while (! car_can_pass(origin, destination)) {
    cv_signal(intersection_cv, intersection_lock);
    cv_wait(intersection_cv, intersection_lock);
  }
  car_passing(origin, destination);
  cv_signal(intersection_cv, intersection_lock);
  lock_release(intersection_lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  lock_acquire(intersection_lock);
  car_passed(origin, destination);
  cv_signal(intersection_cv, intersection_lock);
  lock_release(intersection_lock);
}
