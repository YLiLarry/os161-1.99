#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/*

Solution:

The condition for a car Va to pass is, for all Vb all followings
are true:

• Va.ori = Vb.ori, or
• Va.ori = Vb.dest and Va.dest = Vb.ori, or
• Va.dest != Vb.dest, and at least one of them is making a right turn

For (1) (2) we use 4 integers representing # of cars from origins, 
and 4 integers represeting the same for dests
For (3) we also a integer representing # of cars NOT making a right turn

We also trace how many cars are passing, waiting, and exiting the current
iteration.

Effiency:

For each iteration, we make each car keeps trying and passing at the first
chance they get.

We make sure that each thread should wait for at most one service time per other
thread in the system. To do this we use a iteration_cv which puts the current
iteration threads to sleep after exiting the intersection, and wake them up
after all cars in the current iteration have passed, to start the second
iteration.

*/

static volatile unsigned int origin[4] = {0};
static volatile unsigned int destination[4] = {0};
static volatile unsigned int num_not_right = 0;
static volatile unsigned int passing = 0;
static volatile unsigned int waiting = 0;
static volatile unsigned int exiting = 0;
static struct lock* intersection_lock;
static struct cv* intersection_cv;
static struct cv* iteration_cv;

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
  bool cond = all_from(o) ||
              (all_from(d) && all_to(o)) ||
              (none_to(d) && (is_right_turn(o,d) || num_not_right == 0));
  return cond || passing == 0;
}

static void car_passing(Direction o, Direction d) {
  origin[o]++;
  destination[d]++;
  passing++;
  if (! is_right_turn(o,d)) {
    num_not_right++;
  }
}

static void car_passed(Direction o, Direction d) {
  origin[o]--;
  destination[d]--;
  passing--;
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
  iteration_cv = cv_create("Iteration CV");
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
  cv_destroy(iteration_cv);
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
  // wait until the car can pass
  while (! car_can_pass(origin, destination)) {
    // try next car of the same iteration
    cv_signal(intersection_cv, intersection_lock);
    waiting++;
    cv_wait(intersection_cv, intersection_lock);
    waiting--;
  }
  car_passing(origin, destination);
  // try next car of the same iteration
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
  // when there're cars in the current iteration waiting,
  // ask the next cars to pass
  if (waiting > 0) {
    cv_signal(intersection_cv, intersection_lock);
  }
  // if the current iteration is still running, 
  // wait until the current iteration finishes
  while (waiting + passing > 0) {
    exiting++;
    cv_wait(iteration_cv, intersection_lock);
    exiting--;
  } 
  // the current iteration is clear, wake up next iteration
  cv_signal(iteration_cv, intersection_lock);
  lock_release(intersection_lock);
}
