#ifndef CPU_H
#define CPU_H

#include <linux/types.h>

typedef struct CPUData_ {
  u64 totalTime;
  u64 userTime;
  u64 systemTime;
  u64 systemAllTime;
  u64 idleAllTime;
  u64 idleTime;
  u64 niceTime;
  u64 ioWaitTime;
  u64 irqTime;
  u64 softIrqTime;
  u64 stealTime;
  u64 guestTime;

  u64 totalPeriod;
  u64 userPeriod;
  u64 systemPeriod;
  u64 systemAllPeriod;
  u64 idleAllPeriod;
  u64 idlePeriod;
  u64 nicePeriod;
  u64 ioWaitPeriod;
  u64 irqPeriod;
  u64 softIrqPeriod;
  u64 stealPeriod;
  u64 guestPeriod;
} CPUData;

int get_stat(void);

#define MAXIMUM(x, y) ((x) > (y) ? (x) : (y))
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : MAXIMUM(x, low))

#endif
