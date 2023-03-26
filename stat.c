#include "cpu.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

// #include <linux/cpumask.h> (has already included in linux/kernel_stat.h)
#include <linux/kernel_stat.h>
#include <linux/tick.h>

CPUData *cpus;

static inline void write_into_cpus(u64 usertime, u64 nicetime, u64 systemtime,
                                   u64 idletime, u64 ioWait, u64 irq,
                                   u64 softIrq, u64 steal, u64 guest,
                                   u64 guestnice, u64 cpuid);

/* This subtraction is used by Linux / NetBSD / OpenBSD for calculation of CPU
 * usage items. */
static inline u64 saturatingSub(u64 a, u64 b) { return a > b ? a - b : 0; }

#ifdef arch_idle_time

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu) {
  u64 idle;

  idle = kcs->cpustat[CPUTIME_IDLE];
  if (cpu_online(cpu) && !nr_iowait_cpu(cpu))
    idle += arch_idle_time(cpu);
  return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu) {
  u64 iowait;

  iowait = kcs->cpustat[CPUTIME_IOWAIT];
  if (cpu_online(cpu) && nr_iowait_cpu(cpu))
    iowait += arch_idle_time(cpu);
  return iowait;
}

#else

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu) {
  u64 idle, idle_usecs = -1ULL;

  if (cpu_online(cpu))
    idle_usecs = get_cpu_idle_time_us(cpu, NULL);

  if (idle_usecs == -1ULL)
    /* !NO_HZ or cpu offline so we can rely on cpustat.idle */
    idle = kcs->cpustat[CPUTIME_IDLE];
  else
    idle = idle_usecs * NSEC_PER_USEC;

  return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu) {
  u64 iowait, iowait_usecs = -1ULL;

  if (cpu_online(cpu))
    iowait_usecs = get_cpu_iowait_time_us(cpu, NULL);

  if (iowait_usecs == -1ULL)
    /* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
    iowait = kcs->cpustat[CPUTIME_IOWAIT];
  else
    iowait = iowait_usecs * NSEC_PER_USEC;

  return iowait;
}

#endif

int get_stat(void) {
  // calculate sum data, just add all of possible cpus' data.
  int i;
  u64 usertime, nicetime, systemtime, idletime, ioWait, irq, softIrq, steal;
  u64 guest, guestnice;

  usertime = nicetime = systemtime = idletime = ioWait = irq = softIrq = steal =
      0;
  guest = guestnice = 0;

  for_each_possible_cpu(i) {
    struct kernel_cpustat *kcs = &kcpustat_cpu(i);

    usertime += kcs->cpustat[CPUTIME_USER];
    nicetime += kcs->cpustat[CPUTIME_NICE];
    systemtime += kcs->cpustat[CPUTIME_SYSTEM];
    idletime += get_idle_time(kcs, i);
    ioWait += get_iowait_time(kcs, i);
    irq += kcs->cpustat[CPUTIME_IRQ];
    softIrq += kcs->cpustat[CPUTIME_SOFTIRQ];
    steal += kcs->cpustat[CPUTIME_STEAL];
    guest += kcs->cpustat[CPUTIME_GUEST];
    guestnice += kcs->cpustat[CPUTIME_GUEST_NICE];
  }
  write_into_cpus(usertime, nicetime, systemtime, idletime, ioWait, irq,
                  softIrq, steal, guest, guestnice, num_online_cpus());

  // get each online cpu's data
  for_each_online_cpu(i) {
    struct kernel_cpustat *kcs = &kcpustat_cpu(i);

    /* Copy values here to work around gcc-2.95.3, gcc-2.96 */
    usertime = kcs->cpustat[CPUTIME_USER];
    nicetime = kcs->cpustat[CPUTIME_NICE];
    systemtime = kcs->cpustat[CPUTIME_SYSTEM];
    idletime = get_idle_time(kcs, i);
    ioWait = get_iowait_time(kcs, i);
    irq = kcs->cpustat[CPUTIME_IRQ];
    softIrq = kcs->cpustat[CPUTIME_SOFTIRQ];
    steal = kcs->cpustat[CPUTIME_STEAL];
    guest = kcs->cpustat[CPUTIME_GUEST];
    guestnice = kcs->cpustat[CPUTIME_GUEST_NICE];

    write_into_cpus(usertime, nicetime, systemtime, idletime, ioWait, irq,
                    softIrq, steal, guest, guestnice, i);
  }

  return 0;
}

static inline void write_into_cpus(u64 usertime, u64 nicetime, u64 systemtime,
                                   u64 idletime, u64 ioWait, u64 irq,
                                   u64 softIrq, u64 steal, u64 guest,
                                   u64 guestnice, u64 cpuid) {
  u64 idlealltime, systemalltime, virtalltime, totaltime;
  CPUData *cpuData;
  // Guest time is already accounted in usertime
  usertime -= guest;
  nicetime -= guestnice;
  // Fields existing on kernels >= 2.6
  // (and RHEL's patched kernel 2.4...)
  idlealltime = idletime + ioWait;
  systemalltime = systemtime + irq + softIrq;
  virtalltime = guest + guestnice;
  totaltime =
      usertime + nicetime + systemalltime + idlealltime + steal + virtalltime;
  cpuData = &cpus[cpuid];

  // Since we do a subtraction (usertime - guest) and cputime64_to_clock_t()
  // used in /proc/stat rounds down numbers, it can lead to a case where the
  // integer overflow.
  cpuData->userPeriod = saturatingSub(usertime, cpuData->userTime);
  cpuData->nicePeriod = saturatingSub(nicetime, cpuData->niceTime);
  cpuData->systemPeriod = saturatingSub(systemtime, cpuData->systemTime);
  cpuData->systemAllPeriod =
      saturatingSub(systemalltime, cpuData->systemAllTime);
  cpuData->idleAllPeriod = saturatingSub(idlealltime, cpuData->idleAllTime);
  cpuData->idlePeriod = saturatingSub(idletime, cpuData->idleTime);
  cpuData->ioWaitPeriod = saturatingSub(ioWait, cpuData->ioWaitTime);
  cpuData->irqPeriod = saturatingSub(irq, cpuData->irqTime);
  cpuData->softIrqPeriod = saturatingSub(softIrq, cpuData->softIrqTime);
  cpuData->stealPeriod = saturatingSub(steal, cpuData->stealTime);
  cpuData->guestPeriod = saturatingSub(virtalltime, cpuData->guestTime);
  cpuData->totalPeriod = saturatingSub(totaltime, cpuData->totalTime);
  cpuData->userTime = usertime;
  cpuData->niceTime = nicetime;
  cpuData->systemTime = systemtime;
  cpuData->systemAllTime = systemalltime;
  cpuData->idleAllTime = idlealltime;
  cpuData->idleTime = idletime;
  cpuData->ioWaitTime = ioWait;
  cpuData->irqTime = irq;
  cpuData->softIrqTime = softIrq;
  cpuData->stealTime = steal;
  cpuData->guestTime = virtalltime;
  cpuData->totalTime = totaltime;
}
