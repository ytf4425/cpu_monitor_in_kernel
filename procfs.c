#include "cpu.h"
#include <linux/cpumask.h>
#include <linux/kernel.h>  /* We're doing kernel work */
#include <linux/module.h>  /* Specifically, a module */
#include <linux/proc_fs.h> /* Necessary because we use the proc fs */
#include <linux/string.h>
#include <linux/uaccess.h> /* for copy_from_user */
#include <linux/vmalloc.h>

#define PROCFS_NAME "cpu_threshold"
#define MAX_BUFF_SIZE 2048

extern CPUData *cpus;
extern u64 *alert_threshold;
static char sbuff[MAX_BUFF_SIZE];

static ssize_t proc_read_cpu_threshold(struct file *file, char __user *buffer,
                                       size_t count, loff_t *f_pos);
static ssize_t proc_write_cpu_threshold(struct file *file,
                                        const char __user *buffer, size_t count,
                                        loff_t *f_pos);

static const struct file_operations cpu_threshold_fops = {
    .write = proc_write_cpu_threshold,
    .read = proc_read_cpu_threshold,
    .llseek = noop_llseek,
};

int proc_init(void) {
  proc_create(PROCFS_NAME, 0777, NULL, &cpu_threshold_fops);
  return 0;
}

void proc_exit(void) { remove_proc_entry(PROCFS_NAME, NULL); }

static ssize_t proc_read_cpu_threshold(struct file *file, char __user *buffer,
                                       size_t count, loff_t *f_pos) {
  char str[50] = "";
  int cpuid, max_cpuid;

  max_cpuid = MAX_BUFF_SIZE / 50 >= num_online_cpus() + 1
                  ? num_online_cpus()
                  : MAX_BUFF_SIZE / 50 - 1;
  strcpy(sbuff, "");
  for (cpuid = 0; cpuid < num_online_cpus(); cpuid++) {
    sprintf(str, "CPU %d threshold: %lld %%.\n", cpuid, alert_threshold[cpuid]);
    strcat(sbuff, str);
  }
  sprintf(str, "CPU %d (average usage) threshold: %lld %%.\n", cpuid,
          alert_threshold[cpuid]);
  strcat(sbuff, str);

  count = strlen(sbuff);
  if (*f_pos >= count) {
    return 0;
  }
  count -= *f_pos;
  if (copy_to_user(buffer, sbuff + *f_pos, count)) {
    return -EFAULT;
  }
  *f_pos += count;
  return count;
}

static ssize_t proc_write_cpu_threshold(struct file *file,
                                        const char __user *buffer, size_t count,
                                        loff_t *f_pos) {
  int cpuid, cpu_threshold;
  char str[20] = "";
  count = count < MAX_BUFF_SIZE ? count : MAX_BUFF_SIZE;

  if (copy_from_user(str, buffer, count)) { // error
    printk(KERN_INFO "[cpu_monitor_procfs]: copy_from_user() error!\n");
    return -EFAULT;
  }

  sscanf(str, "%d %d", &cpuid, &cpu_threshold);
  if (cpuid >= 0 && cpuid <= num_online_cpus() && cpu_threshold <= 100 &&
      cpu_threshold >= 0)
    alert_threshold[cpuid] = cpu_threshold;
  return count;
}
