// frame_stubs.h - tiny "counting doorways" for three of Meta's frame functions.
//
// WHAT THIS IS (plain words)
//   The game's VR plugin keeps a list of Meta function addresses (it fills the list itself when the
//   game starts). A "stub" is a small function we put in that list instead. When the plugin calls it,
//   the stub (1) remembers the first 8 argument values and counts the call, then (2) jumps straight
//   on to the REAL Meta function with every register untouched. The real function never knows.
//   Stage D1 only COUNTS. It draws nothing and changes nothing.
#pragma once
#include <stdint.h>

struct FrameWatch {
    const char* name;             // e.g. "ovrp_EndFrame4"
    void* stub;                   // address of our counting stub
    volatile uint64_t* count;     // how many times the game called it
    volatile uint64_t* orig;      // the REAL function address (we set this before patching)
    volatile uint64_t* args;      // the first 8 argument registers of the latest call (8 values)
};

int frameWatchCount();            // how many watches exist (3)
FrameWatch* frameWatch(int i);    // the i-th watch
