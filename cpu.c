#include "cpu.h"
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");

extern CPUData *cpus;
struct timer_list timer;
u64 *alert_threshold;

static inline u64 Platform_setCPUValues(unsigned int cpuid);
extern int proc_init(void);
extern void proc_exit(void);

static inline void timer_task(struct timer_list *arg) {
  int i;
  u64 usage_i;
  int cpu_nums = num_online_cpus();

  get_stat();

  // for each cpu
  for (i = 0; i < cpu_nums; i++) {
    usage_i = Platform_setCPUValues(i);
    if (alert_threshold[i] != 0 && usage_i > alert_threshold[i]) {
      printk(KERN_WARNING
             "WARNING: CPU %d: %llu, larger than alert threshold %llu!",
             i, usage_i, alert_threshold[i]);
    } else {
      printk(KERN_INFO "CPU %d: %llu ", i, usage_i);
    }
  }

  // for avarage CPU usage
  usage_i = Platform_setCPUValues(i);
  if (alert_threshold[i] != 0 && usage_i > alert_threshold[i]) {
    printk(
        KERN_WARNING
        "WARNING: avarage CPU usage: %llu, larger than alert threshold %llu!",
        usage_i, alert_threshold[i]);
  } else {
    printk(KERN_INFO "avarage CPU usage: %llu", usage_i);
  }
  /* Kernel Timer restart */
  mod_timer(&timer, jiffies + msecs_to_jiffies(500));
}

static int __init cpu_monitor_init(void) {
  int cpu_nums = num_online_cpus();
  cpus = (CPUData *)vmalloc(sizeof(CPUData) * (cpu_nums + 1));
  alert_threshold = (u64 *)vmalloc(sizeof(u64) * (cpu_nums + 1));
  memset(cpus, 0, sizeof(CPUData) * (cpu_nums + 1));
  memset(alert_threshold, 0, sizeof(u64) * (cpu_nums + 1));

  get_stat();
  printk(KERN_INFO "cpu monitor init\n");

  // set timer
  timer_setup(&timer, timer_task, 0);
  timer.expires = jiffies + msecs_to_jiffies(500);
  add_timer(&timer);

  // init procfs
  proc_init();

  return 0;
}

static void __exit cpu_monitor_exit(void) {
  vfree(cpus);
  vfree(alert_threshold);
  del_timer(&timer);
  proc_exit();
  printk(KERN_INFO "cpu monitor exit\n");
}

static inline u64 Platform_setCPUValues(unsigned int cpuid) {
  const CPUData *cpuData = &cpus[cpuid];
  u64 total = (cpuData->totalPeriod == 0 ? 1 : cpuData->totalPeriod);

  u64 CPU_METER_NICE = cpuData->nicePeriod * 100 / total;
  u64 CPU_METER_NORMAL = cpuData->userPeriod * 100 / total;
  u64 CPU_METER_KERNEL = cpuData->systemAllPeriod * 100 / total;
  u64 CPU_METER_IRQ =
      (cpuData->stealPeriod + cpuData->guestPeriod) * 100 / total;
  u64 percent =
      CPU_METER_NICE + CPU_METER_NORMAL + CPU_METER_KERNEL + CPU_METER_IRQ;

  percent = CLAMP(percent, 0, 100);

  return percent;
}

module_init(cpu_monitor_init);
module_exit(cpu_monitor_exit);
