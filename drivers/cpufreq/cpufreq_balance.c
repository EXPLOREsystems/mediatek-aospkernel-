/*
 *  drivers/cpufreq/cpufreq_balance.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/tick.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/kthread.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include "mach/hotplug.h"
#include "mach/mt_cpufreq.h"

#include "cpufreq_governor.h"

extern unsigned int get_normal_max_freq(void);
extern unsigned int mt_dvfs_power_dispatch_safe(void);
extern int mt_gpufreq_target(int idx);

void hp_disable_cpu_hp(int disable);
void hp_enable_ambient_mode(int enable);

/* On-demand governor macros */
#define DEF_FREQUENCY_DOWN_DIFFERENTIAL		(10)
#define DEF_FREQUENCY_OD_THRESHOLD          (98)
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_DOWN_DIFFERENTIAL   (15)
#define MIN_FREQUENCY_DOWN_DIFFERENTIAL     (5)
#define MAX_FREQUENCY_DOWN_DIFFERENTIAL     (20)
#define MICRO_FREQUENCY_UP_THRESHOLD        (85)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE     (30000)
#define MIN_FREQUENCY_UP_THRESHOLD          (21)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

#define DEF_CPU_DOWN_DIFFERENTIAL   (10)
#define MICRO_CPU_DOWN_DIFFERENTIAL (10)
#define MIN_CPU_DOWN_DIFFERENTIAL   (0)
#define MAX_CPU_DOWN_DIFFERENTIAL   (30)

#define DEF_CPU_UP_THRESHOLD        (90)
#define MICRO_CPU_UP_THRESHOLD      (90)
#define MIN_CPU_UP_THRESHOLD        (80)
#define MAX_CPU_UP_THRESHOLD        (100)

#define CPU_UP_AVG_TIMES            (10)
#define CPU_DOWN_AVG_TIMES          (50)
#define THERMAL_DISPATCH_AVG_TIMES  (30)

#define DEF_CPU_PERSIST_COUNT   (10)

//#define DEBUG_LOG
#define INPUT_BOOST             (1)


static int vcore_105v;
module_param(vcore_105v, int, 00664);

/* <<<<<<<<<<<<<<<<<<<<<<<<<< */
#ifdef CONFIG_SMP

static int g_next_hp_action = 0;

static long g_cpu_up_sum_load = 0;
static int g_cpu_up_count = 0;

static long g_cpu_down_sum_load = 0;
static int g_cpu_down_count = 0;
static int g_max_cpu_persist_count = 0;
static int g_thermal_count = 0;

static void hp_work_handler(struct work_struct *work);
static struct delayed_work hp_work;

#if INPUT_BOOST
static struct task_struct *freq_up_task;
#endif

#endif

static int cpu_loading = 0;
//static int cpus_sum_load = 0;

static unsigned int dbs_ignore = 1;
static unsigned int dbs_thermal_limited;
static unsigned int dbs_thermal_limited_freq;

/* dvfs thermal limit */
void dbs_freq_thermal_limited(unsigned int limited, unsigned int freq)
{
	dbs_thermal_limited = limited;
	dbs_thermal_limited_freq = freq;
}
EXPORT_SYMBOL(dbs_freq_thermal_limited);

/*
 * dbs_hotplug protects all hotplug related global variables
 */
static DEFINE_MUTEX(hp_mutex);

DEFINE_MUTEX(bl_onoff_mutex);

int g_cpus_sum_load_current = 0;        //set global for information purpose <-XXX

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq);
/* <<<<<<<<<<<<<<<<<<<<<<<<<< */


static DEFINE_PER_CPU(struct bl_cpu_dbs_info_s, bl_cpu_dbs_info);

static struct bl_ops bl_ops;

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_BALANCE
static struct cpufreq_governor cpufreq_gov_balance;
#endif

static unsigned int default_powersave_bias;

static _reset_counters(void)
{
	cpu_loading = 0;
	g_cpus_sum_load_current = 0;

	g_cpu_up_count = 0;
	g_cpu_up_sum_load = 0;

	g_cpu_down_count = 0;
	g_cpu_down_sum_load = 0;

	g_max_cpu_persist_count = 0; 
	g_thermal_count = 0;
}

void cpufreq_min_sampling_rate_change(unsigned int sample_rate)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

    bl_tuners->sampling_rate = sample_rate;
}
EXPORT_SYMBOL(cpufreq_min_sampling_rate_change);

/* <<<<<<<<<<<<<<<<<<<<<<<<<< */
void force_two_core(void)
{
    bool raise_freq = false;
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;
	
    mutex_lock(&hp_mutex);
	if (!bl_tuners->ambient_mode) {
    g_cpu_down_count = 0;
    g_cpu_down_sum_load = 0;
    if (num_online_cpus() < bl_tuners->cpu_num_limit) {
        raise_freq = true;
        g_next_hp_action = 1;
        schedule_delayed_work_on(0, &hp_work, 0);
    }
	}
    mutex_unlock(&hp_mutex);

    if (raise_freq == true) {
		wake_up_process(freq_up_task);
    }

    mt_gpufreq_target(0);
}
/* <<<<<<<<<<<<<<<<<<<<<<<<<< */

static void balance_powersave_bias_init_cpu(int cpu)
{
	struct bl_cpu_dbs_info_s *dbs_info = &per_cpu(bl_cpu_dbs_info, cpu);

	dbs_info->freq_table = cpufreq_frequency_get_table(cpu);
	dbs_info->freq_lo = 0;
}

/*
 * Not all CPUs want IO time to be accounted as busy; this depends on how
 * efficient idling at a higher frequency/voltage is.
 * Pavel Machek says this is not so for various generations of AMD and old
 * Intel systems.
 * Mike Chan (android.com) claims this is also not true for ARM.
 * Because of this, whitelist specific known (series) of CPUs by default, and
 * leave all others up to the user.
 */
static int should_io_be_busy(void)
{
#if defined(CONFIG_X86)
	/*
	 * For Intel, Core 2 (model 15) and later have an efficient idle.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
			boot_cpu_data.x86 == 6 &&
			boot_cpu_data.x86_model >= 15)
		return 1;
#endif
	return 1; // <-XXX
}

/*
 * Find right freq to be set now with powersave_bias on.
 * Returns the freq_hi to be used right now and will set freq_hi_jiffies,
 * freq_lo, and freq_lo_jiffies in percpu area for averaging freqs.
 */
static unsigned int generic_powersave_bias_target(struct cpufreq_policy *policy,
		unsigned int freq_next, unsigned int relation)
{
	unsigned int freq_req, freq_reduc, freq_avg;
	unsigned int freq_hi, freq_lo;
	unsigned int index = 0;
	unsigned int jiffies_total, jiffies_hi, jiffies_lo;
	struct bl_cpu_dbs_info_s *dbs_info = &per_cpu(bl_cpu_dbs_info,
						   policy->cpu);
	struct dbs_data *dbs_data = policy->governor_data;
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;

	if (!dbs_info->freq_table) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_next;
	}

	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_next,
			relation, &index);
	freq_req = dbs_info->freq_table[index].frequency;
	freq_reduc = freq_req * bl_tuners->powersave_bias / 1000;
	freq_avg = freq_req - freq_reduc;

	/* Find freq bounds for freq_avg in freq_table */
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_H, &index);
	freq_lo = dbs_info->freq_table[index].frequency;
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_L, &index);
	freq_hi = dbs_info->freq_table[index].frequency;

	/* Find out how long we have to be in hi and lo freqs */
	if (freq_hi == freq_lo) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_lo;
	}
	jiffies_total = usecs_to_jiffies(bl_tuners->sampling_rate);
	jiffies_hi = (freq_avg - freq_lo) * jiffies_total;
	jiffies_hi += ((freq_hi - freq_lo) / 2);
	jiffies_hi /= (freq_hi - freq_lo);
	jiffies_lo = jiffies_total - jiffies_hi;
	dbs_info->freq_lo = freq_lo;
	dbs_info->freq_lo_jiffies = jiffies_lo;
	dbs_info->freq_hi_jiffies = jiffies_hi;
	return freq_hi;
}

static void balance_powersave_bias_init(void)
{
	int i;
	for_each_online_cpu(i) {
		balance_powersave_bias_init_cpu(i);
	}
}

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq)
{
	struct dbs_data *dbs_data = p->governor_data;
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;

	if (bl_tuners->powersave_bias)
		freq = bl_ops.powersave_bias_target(p, freq,
				CPUFREQ_RELATION_H);
	else if (p->cur == p->max)
	{
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<  ?????
		if (dbs_ignore == 0)
			dbs_ignore = 1;
		else
			return;
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
	}
	
	__cpufreq_driver_target(p, freq, bl_tuners->powersave_bias ?
			CPUFREQ_RELATION_L : CPUFREQ_RELATION_H);
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void hp_disable_cpu_hp(int disable)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

	#if 1
	mutex_lock(&hp_mutex);
	bl_tuners->is_cpu_hotplug_disable = disable;
	mutex_unlock(&hp_mutex);
	#endif
}
EXPORT_SYMBOL(hp_disable_cpu_hp);

void hp_enable_ambient_mode(int enable)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

	#if 1
	mutex_lock(&hp_mutex);
	mt_cpufreq_set_ambient_mode(enable);
	bl_tuners->ambient_mode = enable;
	mutex_unlock(&hp_mutex);
	#endif
}
EXPORT_SYMBOL(hp_enable_ambient_mode);


int mt_cpufreq_cur_load(void)
{
    return cpu_loading;
}
EXPORT_SYMBOL(mt_cpufreq_cur_load);

void hp_set_dynamic_cpu_hotplug_enable(int enable)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

	mutex_lock(&hp_mutex);
	bl_tuners->is_cpu_hotplug_disable = !enable;
	mutex_unlock(&hp_mutex);
}
EXPORT_SYMBOL(hp_set_dynamic_cpu_hotplug_enable);

void hp_limited_cpu_num(int num)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

	mutex_lock(&hp_mutex);
	bl_tuners->cpu_num_limit = num;

	if (num < num_online_cpus()) {
		pr_info("%s: CPU off due to thermal protection! limit_num = %d < online = %d\n", 
                    __func__, num, num_online_cpus());
		g_next_hp_action = 0;
		schedule_delayed_work_on(0, &hp_work, 0);
		g_cpu_down_count = 0;
		g_cpu_down_sum_load = 0;
	}
    
	mutex_unlock(&hp_mutex);
}
EXPORT_SYMBOL(hp_limited_cpu_num);

void hp_based_cpu_num(int num)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

	mutex_lock(&hp_mutex);
	bl_tuners->cpu_num_base = num;
	mutex_unlock(&hp_mutex);
}
EXPORT_SYMBOL(hp_based_cpu_num);

#ifdef CONFIG_SMP

static void hp_work_handler(struct work_struct *work)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners;

	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;

	if (mutex_trylock(&bl_onoff_mutex))
	{
		if (!bl_tuners->is_cpu_hotplug_disable)
		{
			int onlines_cpu_n = num_online_cpus();

			if (g_next_hp_action) // turn on CPU
			{
				if (onlines_cpu_n < num_possible_cpus())
				{
					pr_debug("hp_work_handler: cpu_up(%d) kick off\n", onlines_cpu_n);
					cpu_up(onlines_cpu_n);
					pr_debug("hp_work_handler: cpu_up(%d) completion\n", onlines_cpu_n);

					dbs_ignore = 0; // force trigger frequency scaling
				}
			}
			else // turn off CPU
			{
				if (onlines_cpu_n > 1)
				{
					pr_debug("hp_work_handler: cpu_down(%d) kick off\n", (onlines_cpu_n - 1));
					cpu_down((onlines_cpu_n - 1));
					pr_debug("hp_work_handler: cpu_down(%d) completion\n", (onlines_cpu_n - 1));

					dbs_ignore = 0; // force trigger frequency scaling
				}
			}
		}
		mutex_unlock(&bl_onoff_mutex);
	}
}

#endif
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency. Every sampling_rate, we look
 * for the lowest frequency which can sustain the load while keeping idle time
 * over 30%. If such a frequency exist, we try to decrease to this frequency.
 *
 * Any frequency increase takes it to the maximum frequency. Frequency reduction
 * happens at minimum steps of 5% (default) of current frequency
 */
static void bl_check_cpu(int cpu, unsigned int load_freq)
{
	struct bl_cpu_dbs_info_s *dbs_info = &per_cpu(bl_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	struct dbs_data *dbs_data = policy->governor_data;
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	bool raise_freq = false;
	
	dbs_info->freq_lo = 0;

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    // record loading information
    cpu_loading = load_freq / policy->cur;
    // dispatch power budget
    if(g_thermal_count >= bl_tuners->thermal_dispatch_avg_times) {
        g_thermal_count = 0;
        mt_dvfs_power_dispatch_safe();
        if ((dbs_thermal_limited == 1) && (policy->cur > dbs_thermal_limited_freq))
    		__cpufreq_driver_target(policy, dbs_thermal_limited_freq, CPUFREQ_RELATION_L);
    }
    else
        g_thermal_count++;
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    if (policy->cur >= get_normal_max_freq()){
        if ((load_freq > bl_tuners->od_threshold * policy->cur) && (num_online_cpus() == num_possible_cpus())){
            g_max_cpu_persist_count++;
			#ifdef DEBUG_LOG
            pr_debug("dvfs_od: g_max_cpu_persist_count: %d\n", g_max_cpu_persist_count);
			#endif
            if(g_max_cpu_persist_count == DEF_CPU_PERSIST_COUNT){
                //only ramp up to OD OPP here
				#ifdef DEBUG_LOG
		        pr_debug("dvfs_od: cpu loading = %d\n", load_freq/policy->cur);
				#endif
                if (policy->cur < policy->max)
                    dbs_info->rate_mult =
                        bl_tuners->sampling_down_factor;
                dbs_freq_increase(policy, policy->max);
				#ifdef DEBUG_LOG
                pr_debug("reset g_max_cpu_persist_count, count = 10\n");
				#endif
                g_max_cpu_persist_count = 0;
                goto hp_check;
            }
        }
        else {
            g_max_cpu_persist_count = 0;
        }
    }
    else{
		 if (load_freq > bl_tuners->up_threshold * policy->cur) {
			 /* If switching to max speed, apply sampling_down_factor */
			 if (policy->cur < get_normal_max_freq())
				 dbs_info->rate_mult =
					 bl_tuners->sampling_down_factor;
			 dbs_freq_increase(policy, get_normal_max_freq());
			 if(g_max_cpu_persist_count != 0){
				 g_max_cpu_persist_count = 0;
		 		#ifdef DEBUG_LOG
				 pr_debug("reset g_max_cpu_persist_count, and fallback to normal max\n");
		 		#endif
			 }
			 goto hp_check;
		}
    }
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

	#if 0
	/* Check for frequency increase */
	if (load_freq > bl_tuners->up_threshold * policy->cur) {
		/* If switching to max speed, apply sampling_down_factor */
		if (policy->cur < policy->max)
			dbs_info->rate_mult =
				bl_tuners->sampling_down_factor;
		dbs_freq_increase(policy, policy->max);
		goto hp_check;   // <-XXX
	}
	#endif
	
	/* Check for frequency decrease */
	/* if we cannot reduce the frequency anymore, break out early */
	if (policy->cur == policy->min)
		goto hp_check;   // <-XXX

	/*
	 * The optimal frequency is the frequency that is the lowest that can
	 * support the current CPU usage without triggering the up policy. To be
	 * safe, we focus 10 points under the threshold.
	 */
	if (load_freq < bl_tuners->adj_up_threshold
			* policy->cur) {
		unsigned int freq_next;
		freq_next = load_freq / bl_tuners->adj_up_threshold;

		/* No longer fully busy, reset rate_mult */
		dbs_info->rate_mult = 1;

		if (freq_next < policy->min)
			freq_next = policy->min;

		// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        if(g_max_cpu_persist_count != 0){
            g_max_cpu_persist_count = 0;
			#ifdef DEBUG_LOG
            pr_debug("reset g_max_cpu_persist_count, decrease freq accrording to loading\n");
			#endif
        }
		// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
		
		if (!bl_tuners->powersave_bias) {
			__cpufreq_driver_target(policy, freq_next,
					CPUFREQ_RELATION_L);

		} else {

			freq_next = bl_ops.powersave_bias_target(policy, freq_next,
						CPUFREQ_RELATION_L);
			__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_L);
		}
	}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
hp_check:

	/* If Hot Plug policy disable, return directly */
	if (bl_tuners->is_cpu_hotplug_disable)
		return;

	#ifdef CONFIG_SMP
	mutex_lock(&hp_mutex);

	/* Check CPU loading to power up slave CPU */
	if (num_online_cpus() < bl_tuners->cpu_num_base && num_online_cpus() < bl_tuners->cpu_num_limit) {
		raise_freq = true;
		pr_debug("dbs_check_cpu: turn on CPU by perf service\n");
		g_next_hp_action = 1;
		schedule_delayed_work_on(0, &hp_work, 0);
	} else if (num_online_cpus() < num_possible_cpus() && num_online_cpus() < bl_tuners->cpu_num_limit) {
		g_cpu_up_count++;
		g_cpu_up_sum_load += g_cpus_sum_load_current;
		if (g_cpu_up_count == bl_tuners->cpu_up_avg_times) {
			g_cpu_up_sum_load /= bl_tuners->cpu_up_avg_times;
			if (g_cpu_up_sum_load > 
				(bl_tuners->cpu_up_threshold * num_online_cpus())) {
				#ifdef DEBUG_LOG
				pr_debug("dbs_check_cpu: g_cpu_up_sum_load = %d\n", g_cpu_up_sum_load);
				#endif
				raise_freq = true;
				pr_debug("dbs_check_cpu: turn on CPU\n");
				g_next_hp_action = 1;
				schedule_delayed_work_on(0, &hp_work, 0);
			}
			g_cpu_up_count = 0;
			g_cpu_up_sum_load = 0;
		}
		#ifdef DEBUG_LOG
		pr_debug("dbs_check_cpu: g_cpu_up_count = %d, g_cpu_up_sum_load = %d\n", g_cpu_up_count, g_cpu_up_sum_load);
		pr_debug("dbs_check_cpu: cpu_up_threshold = %d\n", (bl_tuners->cpu_up_threshold * num_online_cpus()));
		#endif
	}

	/* Check CPU loading to power down slave CPU */
	if (num_online_cpus() > 1) {
		g_cpu_down_count++;
		g_cpu_down_sum_load += g_cpus_sum_load_current;
		if (g_cpu_down_count == bl_tuners->cpu_down_avg_times) {
			g_cpu_down_sum_load /= bl_tuners->cpu_down_avg_times;
			if (g_cpu_down_sum_load < 
				((bl_tuners->cpu_up_threshold - bl_tuners->cpu_down_differential) * (num_online_cpus() - 1))) {
				if (num_online_cpus() > bl_tuners->cpu_num_base) {
				#ifdef DEBUG_LOG
				pr_debug("dbs_check_cpu: g_cpu_down_sum_load = %d\n", g_cpu_down_sum_load);
				#endif
				raise_freq = true;
				pr_debug("dbs_check_cpu: turn off CPU\n");
				g_next_hp_action = 0;
				schedule_delayed_work_on(0, &hp_work, 0);
				}
			}
			g_cpu_down_count = 0;
			g_cpu_down_sum_load = 0;
		}
		#ifdef DEBUG_LOG
		pr_debug("dbs_check_cpu: g_cpu_down_count = %d, g_cpu_down_sum_load = %d\n", g_cpu_down_count, g_cpu_down_sum_load);
		pr_debug("dbs_check_cpu: cpu_down_threshold = %d\n", ((bl_tuners->cpu_up_threshold - bl_tuners->cpu_down_differential) * (num_online_cpus() - 1)));
		#endif
	}

	mutex_unlock(&hp_mutex);
	#endif
	// need to retrieve dbs_freq_increase out of hp_mutex
	// in case of self-deadlock 
	if(raise_freq == true)
		dbs_freq_increase(policy, policy->max);

	return;	
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

}

static void bl_dbs_timer(struct work_struct *work)
{
	struct bl_cpu_dbs_info_s *dbs_info =
		container_of(work, struct bl_cpu_dbs_info_s, cdbs.work.work);
	unsigned int cpu = dbs_info->cdbs.cur_policy->cpu;
	struct bl_cpu_dbs_info_s *core_dbs_info = &per_cpu(bl_cpu_dbs_info,
			cpu);
	struct dbs_data *dbs_data = dbs_info->cdbs.cur_policy->governor_data;
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	int delay = 0, sample_type = core_dbs_info->sample_type;
	bool modify_all = true;

	mutex_lock(&core_dbs_info->cdbs.timer_mutex);
	if (!need_load_eval(&core_dbs_info->cdbs, bl_tuners->sampling_rate)) {
		modify_all = false;
		goto max_delay;
	}

	/* Common NORMAL_SAMPLE setup */
	core_dbs_info->sample_type = BL_NORMAL_SAMPLE;
	if (sample_type == BL_SUB_SAMPLE) {
		delay = core_dbs_info->freq_lo_jiffies;
		__cpufreq_driver_target(core_dbs_info->cdbs.cur_policy,
				core_dbs_info->freq_lo, CPUFREQ_RELATION_H);
	} else {
		dbs_check_cpu(dbs_data, cpu);
		if (core_dbs_info->freq_lo) {
			/* Setup timer for SUB_SAMPLE */
			core_dbs_info->sample_type = BL_SUB_SAMPLE;
			delay = core_dbs_info->freq_hi_jiffies;
		}
	}

max_delay:
	if (!delay)
		delay = delay_for_sampling_rate(bl_tuners->sampling_rate
				* core_dbs_info->rate_mult);

	gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy, delay, modify_all);
	mutex_unlock(&core_dbs_info->cdbs.timer_mutex);
}

/************************** sysfs interface ************************/
static struct common_dbs_data bl_dbs_cdata;

/**
 * update_sampling_rate - update sampling rate effective immediately if needed.
 * @new_rate: new sampling rate
 *
 * If new rate is smaller than the old, simply updating
 * dbs_tuners_int.sampling_rate might not be appropriate. For example, if the
 * original sampling_rate was 1 second and the requested new sampling rate is 10
 * ms because the user needs immediate reaction from ondemand governor, but not
 * sure if higher frequency will be required or not, then, the governor may
 * change the sampling rate too late; up to 1 second later. Thus, if we are
 * reducing the sampling rate, we need to make the new value effective
 * immediately.
 */
static void update_sampling_rate(struct dbs_data *dbs_data,
		unsigned int new_rate)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	int cpu = 0; // XXX: FIX CPU0 for cdbs.work

	bl_tuners->sampling_rate = new_rate = max(new_rate,
			dbs_data->min_sampling_rate);

	//for_each_online_cpu(cpu) {
	{
		struct cpufreq_policy *policy;
		struct bl_cpu_dbs_info_s *dbs_info;
		unsigned long next_sampling, appointed_at;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			return;
		if (policy->governor != &cpufreq_gov_balance) {
			cpufreq_cpu_put(policy);
			return;
		}
		dbs_info = &per_cpu(bl_cpu_dbs_info, cpu);
		cpufreq_cpu_put(policy);

		mutex_lock(&dbs_info->cdbs.timer_mutex);

		if (!delayed_work_pending(&dbs_info->cdbs.work)) {
			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			return;
		}

		next_sampling = jiffies + usecs_to_jiffies(new_rate);
		appointed_at = dbs_info->cdbs.work.timer.expires;

		if (time_before(next_sampling, appointed_at)) {

			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			cancel_delayed_work_sync(&dbs_info->cdbs.work);// XXX: FIX CPU0 for cdbs.work
			mutex_lock(&dbs_info->cdbs.timer_mutex);

			gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy,
					usecs_to_jiffies(new_rate), true);

		}
		mutex_unlock(&dbs_info->cdbs.timer_mutex);
	}
}

void bl_enable_timer(int enable)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data;
	static unsigned int sampling_rate_backup = 0;

	if (!dbs_data || dbs_data->cdata->governor != GOV_BALANCE || (enable && !sampling_rate_backup))
		return;

	if (enable)
		update_sampling_rate(dbs_data, sampling_rate_backup);
	else {
        	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
        	struct cpufreq_policy *policy;
		struct bl_cpu_dbs_info_s *dbs_info;
		unsigned int new_rate = 30000 * 100;  // change to 3s

		/* restore original sampling rate */
		sampling_rate_backup = bl_tuners->sampling_rate;
		update_sampling_rate(dbs_data, new_rate);

		/* cancel work in workqueue and start new work */
		policy = cpufreq_cpu_get(0);
		if (!policy)
			return;

		dbs_info = &per_cpu(bl_cpu_dbs_info, 0);
		cpufreq_cpu_put(policy);

		mutex_lock(&dbs_info->cdbs.timer_mutex);

		if (!delayed_work_pending(&dbs_info->cdbs.work)) {
			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			return;
		}

		mutex_unlock(&dbs_info->cdbs.timer_mutex);

		cancel_delayed_work_sync(&dbs_info->cdbs.work);// XXX: FIX CPU0 for cdbs.work
		
		mutex_lock(&dbs_info->cdbs.timer_mutex);
		gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy,
					usecs_to_jiffies(new_rate), true);
		mutex_unlock(&dbs_info->cdbs.timer_mutex);
	}
}
EXPORT_SYMBOL(bl_enable_timer);

static ssize_t store_sampling_rate(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	update_sampling_rate(dbs_data, input);
	return count;
}

static ssize_t store_io_is_busy(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	bl_tuners->io_is_busy = !!input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct bl_cpu_dbs_info_s *dbs_info = &per_cpu(bl_cpu_dbs_info,
									j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
			&dbs_info->cdbs.prev_cpu_wall, bl_tuners->io_is_busy);
	}
	return count;
}

static ssize_t store_up_threshold(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	/* Calculate the new adj_up_threshold */
	bl_tuners->adj_up_threshold += input;
	bl_tuners->adj_up_threshold -= bl_tuners->up_threshold;

	bl_tuners->up_threshold = input;
	return count;
}

static ssize_t store_sampling_down_factor(struct dbs_data *dbs_data,
		const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input, j;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;
	bl_tuners->sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct bl_cpu_dbs_info_s *dbs_info = &per_cpu(bl_cpu_dbs_info,
				j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

static ssize_t store_ignore_nice_load(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == bl_tuners->ignore_nice_load) { /* nothing to do */
		return count;
	}
	bl_tuners->ignore_nice_load = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct bl_cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(bl_cpu_dbs_info, j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
			&dbs_info->cdbs.prev_cpu_wall, bl_tuners->io_is_busy);
		if (bl_tuners->ignore_nice_load)
			dbs_info->cdbs.prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];

	}
	return count;
}

static ssize_t store_powersave_bias(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 1000)
		input = 1000;

	bl_tuners->powersave_bias = input;
	balance_powersave_bias_init();
	return count;
}

show_store_one(bl, sampling_rate);
show_store_one(bl, io_is_busy);
show_store_one(bl, up_threshold);
show_store_one(bl, sampling_down_factor);
show_store_one(bl, ignore_nice_load);
show_store_one(bl, powersave_bias);
declare_show_sampling_rate_min(bl);

gov_sys_pol_attr_rw(sampling_rate);
gov_sys_pol_attr_rw(io_is_busy);
gov_sys_pol_attr_rw(up_threshold);
gov_sys_pol_attr_rw(sampling_down_factor);
gov_sys_pol_attr_rw(ignore_nice_load);
gov_sys_pol_attr_rw(powersave_bias);
gov_sys_pol_attr_ro(sampling_rate_min);


/* <<<<<<<<<<<<<<<<<<<<<<<<<< */
static ssize_t store_od_threshold(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	bl_tuners->od_threshold = input;
	return count;
}

static ssize_t store_down_differential(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_DOWN_DIFFERENTIAL ||
			input < MIN_FREQUENCY_DOWN_DIFFERENTIAL) {
		return -EINVAL;
	}
	bl_tuners->down_differential = input;
	return count;
}

static ssize_t store_cpu_up_threshold(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_CPU_UP_THRESHOLD ||
		input < MIN_CPU_UP_THRESHOLD) {
		return -EINVAL;
	}
	bl_tuners->cpu_up_threshold = input;
	return count;
}

static ssize_t store_cpu_down_differential(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_CPU_DOWN_DIFFERENTIAL ||
		input < MIN_CPU_DOWN_DIFFERENTIAL) {
		return -EINVAL;
	}
	bl_tuners->cpu_down_differential = input;
	return count;
}

static ssize_t store_cpu_up_avg_times(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	bl_tuners->cpu_up_avg_times = input;
	return count;
}

static ssize_t store_cpu_down_avg_times(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	bl_tuners->cpu_down_avg_times = input;
	return count;
}

static ssize_t store_thermal_dispatch_avg_times(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	bl_tuners->thermal_dispatch_avg_times = input;
	return count;
}

static ssize_t store_cpu_num_limit(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	bl_tuners->cpu_num_limit = input;
	return count;
}

static ssize_t store_cpu_num_base(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	bool raise_freq = false;
	int ret;
	struct cpufreq_policy *policy;
	
	mutex_lock(&hp_mutex);
	if (bl_tuners->ambient_mode) {
		mutex_unlock(&hp_mutex);
		return count;
	}
	mutex_unlock(&hp_mutex);
	
	policy = cpufreq_cpu_get(0);
	ret = sscanf(buf, "%u", &input);

	bl_tuners->cpu_num_base = input;
	mutex_lock(&hp_mutex);
	if (num_online_cpus() < bl_tuners->cpu_num_base && num_online_cpus() < bl_tuners->cpu_num_limit) {
		raise_freq = true;
		g_next_hp_action = 1;
		schedule_delayed_work_on(0, &hp_work, 0);
	}
	mutex_unlock(&hp_mutex);
	
	if(raise_freq == true)
		dbs_freq_increase(policy, policy->max);
	
	return count;
}

static ssize_t store_is_cpu_hotplug_disable(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	bl_tuners->is_cpu_hotplug_disable = input;
	return count;
}

#if INPUT_BOOST
static ssize_t store_cpu_input_boost_enable(struct dbs_data *dbs_data, const char *buf, size_t count)
{
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 1 ||
		input < 0) {
		return -EINVAL;
	}

	mutex_lock(&hp_mutex);
	bl_tuners->cpu_input_boost_enable = input;
	mutex_unlock(&hp_mutex);

	return count;
}
#endif


static int _ambient_mode = 0;
static int ambient_mode_suspend(struct platform_device *dev, pm_message_t state)
{
	if (_ambient_mode) {
		//pr_debug("[%s] enter online = %d\n", __func__, num_online_cpus());
		disable_nonboot_cpus();
		//pr_debug("[%s] leave online = %d\n", __func__, num_online_cpus());
		_reset_counters();
	}

	return 0;
}

static struct platform_driver ambient_mode_pdriver = {
	.suspend    = ambient_mode_suspend,
	.driver     = {
		.name   = "ambient_mode",
		.owner  = THIS_MODULE,
	},
};

static int __init ambient_mode_init(void)
{
	struct platform_device *pdev;
	int ret;

	ret = platform_driver_register(&ambient_mode_pdriver);
	if (ret) {
		pr_err("Register ambient mode platform driver failed %d\n", ret);
		return -1 * __LINE__;
	}

	pdev = platform_device_alloc("ambient_mode", -1);
	if (!pdev) {
		pr_err("%s: Failed to device alloc for ambient_mode\n", __func__);
		return -ENOMEM;
	}
	ret = platform_device_add(pdev);
	if (ret < 0) {
		platform_device_put(pdev);
		pdev = NULL;
	}

	return ret;
}
module_init(ambient_mode_init);


static ssize_t store_ambient_mode(struct dbs_data *dbs_data, const char *buf, size_t count)
{
#if 1
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return count;

	if (_ambient_mode == !!input)
		return count;
	_ambient_mode = !!input;

	if (input) {
		pr_debug("ambient mode enabled %d\n", num_online_cpus());
	} else {
		pr_debug("ambient mode disabled %d\n", num_online_cpus());
	}

	return count;
#else
	struct bl_cpu_dbs_info_s *dbs_info;
	struct bl_dbs_tuners *bl_tuners = dbs_data->tuners;
	unsigned int input;
	int ret, i;
	struct cpufreq_policy *policy;
	static unsigned int backup_dvfs_wdata1;

	policy = cpufreq_cpu_get(0);
	dbs_info = &per_cpu(bl_cpu_dbs_info, policy->cpu);
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return count;

	if (bl_tuners->ambient_mode == !!input)
		return count;

	//pr_debug("########## store_ambient_mode, input = %d ##########\n", input);
	
	/* Make sure hotplug workqueue is not running at the same time */
	if (input) {
		/* Cancel the timer. */
		mutex_lock(&dbs_info->cdbs.timer_mutex);
		cancel_delayed_work_sync(&dbs_info->cdbs.work);
		mutex_unlock(&dbs_info->cdbs.timer_mutex);

		mutex_lock(&bl_onoff_mutex);
		hp_enable_ambient_mode(1);
		/**
		 * No need to cancel hp_work.
		 * If it is pending, we have disabled hotplug here. 
		 * Thus it will do nothing.
		 * If it is running, we already acquired bl_onoff_mutex.
		 */
		hp_disable_cpu_hp(1);
		for (i = (num_possible_cpus() - 1); i > 0; i--)
		{
			if (cpu_online(i))
				cpu_down(i);
		}
		mutex_unlock(&bl_onoff_mutex);
		dbs_ignore = 0;
		dbs_freq_increase(policy, policy->min);

		if (vcore_105v) {
			/* Change table index 1 to 1.05V */
			backup_dvfs_wdata1 = *(volatile unsigned int *)PMIC_WRAP_DVFS_WDATA1;
			mt65xx_reg_sync_writel(0x38, PMIC_WRAP_DVFS_WDATA1);
			/* FIXME: g_volt in mt_gpufreq.c will still be 1.15v */
			/* FIXME: g_cur_volt in mt_gpufreq.c will still be 1.15v */
			/* spm_pmic_config[1].cpufreq_volt is not updated */
			spm_dvfs_ctrl_volt(1);
		}
	} else {
		int delay;

		if (vcore_105v) {
			/* Restore table index 1 value (1.15V) */
			mt65xx_reg_sync_writel(backup_dvfs_wdata1, PMIC_WRAP_DVFS_WDATA1);
			spm_dvfs_ctrl_volt(1);
		}

		_reset_counters();
		hp_enable_ambient_mode(0);
		hp_disable_cpu_hp(0);

		delay = usecs_to_jiffies(bl_tuners->sampling_rate);
		gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy, delay, true);
	}
	
	return count;
#endif
}

show_store_one(bl, od_threshold);
show_store_one(bl, down_differential);
show_store_one(bl, cpu_up_threshold);
show_store_one(bl, cpu_down_differential);
show_store_one(bl, cpu_up_avg_times);
show_store_one(bl, cpu_down_avg_times);
show_store_one(bl, thermal_dispatch_avg_times);
show_store_one(bl, cpu_num_limit);
show_store_one(bl, cpu_num_base);
show_store_one(bl, is_cpu_hotplug_disable);
#if INPUT_BOOST
show_store_one(bl, cpu_input_boost_enable);
#endif
show_store_one(bl, ambient_mode);

gov_sys_pol_attr_rw(od_threshold);
gov_sys_pol_attr_rw(down_differential);
gov_sys_pol_attr_rw(cpu_up_threshold);
gov_sys_pol_attr_rw(cpu_down_differential);
gov_sys_pol_attr_rw(cpu_up_avg_times);
gov_sys_pol_attr_rw(cpu_down_avg_times);
gov_sys_pol_attr_rw(thermal_dispatch_avg_times);
gov_sys_pol_attr_rw(cpu_num_limit);
gov_sys_pol_attr_rw(cpu_num_base);
gov_sys_pol_attr_rw(is_cpu_hotplug_disable);
#if INPUT_BOOST
gov_sys_pol_attr_rw(cpu_input_boost_enable);
#endif
gov_sys_pol_attr_rw(ambient_mode);
/* <<<<<<<<<<<<<<<<<<<<<<<<<< */

static struct attribute *dbs_attributes_gov_sys[] = {
	&sampling_rate_min_gov_sys.attr,
	&sampling_rate_gov_sys.attr,
	&up_threshold_gov_sys.attr,
	&sampling_down_factor_gov_sys.attr,
	&ignore_nice_load_gov_sys.attr,
	&powersave_bias_gov_sys.attr,
	&io_is_busy_gov_sys.attr,
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
	&od_threshold_gov_sys.attr,
	&down_differential_gov_sys.attr,
	&cpu_up_threshold_gov_sys.attr,
	&cpu_down_differential_gov_sys.attr,
	&cpu_up_avg_times_gov_sys.attr,
	&cpu_down_avg_times_gov_sys.attr,
	&thermal_dispatch_avg_times_gov_sys.attr,
	&cpu_num_limit_gov_sys.attr,
	&cpu_num_base_gov_sys.attr,
	&is_cpu_hotplug_disable_gov_sys.attr,
	#if INPUT_BOOST
	&cpu_input_boost_enable_gov_sys.attr,
	#endif
	&ambient_mode_gov_sys.attr,
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	NULL
};

static struct attribute_group bl_attr_group_gov_sys = {
	.attrs = dbs_attributes_gov_sys,
	.name = "balance",
};

static struct attribute *dbs_attributes_gov_pol[] = {
	&sampling_rate_min_gov_pol.attr,
	&sampling_rate_gov_pol.attr,
	&up_threshold_gov_pol.attr,
	&sampling_down_factor_gov_pol.attr,
	&ignore_nice_load_gov_pol.attr,
	&powersave_bias_gov_pol.attr,
	&io_is_busy_gov_pol.attr,
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
	&od_threshold_gov_pol.attr,
	&down_differential_gov_pol.attr,
	&cpu_up_threshold_gov_pol.attr,
	&cpu_down_differential_gov_pol.attr,
	&cpu_up_avg_times_gov_pol.attr,
	&cpu_down_avg_times_gov_pol.attr,
	&thermal_dispatch_avg_times_gov_pol.attr,
	&cpu_num_limit_gov_pol.attr,
	&cpu_num_base_gov_pol.attr,
	&is_cpu_hotplug_disable_gov_pol.attr,
	#if INPUT_BOOST
	&cpu_input_boost_enable_gov_pol.attr,
	#endif
	&ambient_mode_gov_pol.attr,
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	NULL
};

static struct attribute_group bl_attr_group_gov_pol = {
	.attrs = dbs_attributes_gov_pol,
	.name = "balance",
};

/************************** sysfs end ************************/

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#if INPUT_BOOST
static int touch_freq_up_task(void *data)
{
	struct cpufreq_policy *policy;

	while (1) {
		policy = cpufreq_cpu_get(0);
		if(policy != NULL)
		{
            dbs_freq_increase(policy, policy->max);
            cpufreq_cpu_put(policy);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();

		if (kthread_should_stop())
			break;
	}

	return 0;
}

static void dbs_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	struct dbs_data *dbs_data = per_cpu(bl_cpu_dbs_info, 0).cdbs.cur_policy->governor_data; // <-XXX
	struct bl_dbs_tuners *bl_tuners;
	
	if (!dbs_data)
		return;
	bl_tuners = dbs_data->tuners;
	if (!bl_tuners)
		return;
	
	if ((type == EV_KEY) && (code == BTN_TOUCH) && (value == 1) && (bl_tuners->cpu_input_boost_enable))
	{
		force_two_core();
	}
}

static int dbs_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq_balance";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void dbs_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id dbs_ids[] = {
        {
                .flags = INPUT_DEVICE_ID_MATCH_EVBIT |
                         INPUT_DEVICE_ID_MATCH_ABSBIT,
                .evbit = { BIT_MASK(EV_ABS) },
                .absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
                            BIT_MASK(ABS_MT_POSITION_X) |
                            BIT_MASK(ABS_MT_POSITION_Y) },
        }, /* multi-touch touchscreen */
        {
                .flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
                         INPUT_DEVICE_ID_MATCH_ABSBIT,
                .keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
                .absbit = { [BIT_WORD(ABS_X)] =
                            BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
        }, /* touchpad */
        { },
};

static struct input_handler dbs_input_handler = {
	.event		= dbs_input_event,
	.connect	= dbs_input_connect,
	.disconnect	= dbs_input_disconnect,
	.name		= "cpufreq_balance",
	.id_table	= dbs_ids,
};
#endif //#ifdef CONFIG_HOTPLUG_CPU
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

static int bl_init(struct dbs_data *dbs_data)
{
	struct bl_dbs_tuners *tuners;
	u64 idle_time;
	int cpu;

	tuners = kzalloc(sizeof(struct bl_dbs_tuners), GFP_KERNEL);
	if (!tuners) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	cpu = get_cpu();
	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		tuners->up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		tuners->adj_up_threshold = MICRO_FREQUENCY_UP_THRESHOLD -
			MICRO_FREQUENCY_DOWN_DIFFERENTIAL;
		tuners->down_differential = MICRO_FREQUENCY_DOWN_DIFFERENTIAL; // <-XXX
		tuners->cpu_up_threshold = MICRO_CPU_UP_THRESHOLD; // <-XXX
		tuners->cpu_down_differential = MICRO_CPU_DOWN_DIFFERENTIAL; // <-XXX

		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		dbs_data->min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		tuners->up_threshold = DEF_FREQUENCY_UP_THRESHOLD;
		tuners->adj_up_threshold = DEF_FREQUENCY_UP_THRESHOLD -
			DEF_FREQUENCY_DOWN_DIFFERENTIAL;
		tuners->down_differential = DEF_FREQUENCY_DOWN_DIFFERENTIAL; // <-XXX
		tuners->cpu_up_threshold = DEF_CPU_UP_THRESHOLD; // <-XXX
		tuners->cpu_down_differential = DEF_CPU_DOWN_DIFFERENTIAL; // <-XXX

		/* For correct statistics, we need 10 ticks for each measure */
		dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
			jiffies_to_usecs(10);
	}

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
	tuners->od_threshold = DEF_FREQUENCY_OD_THRESHOLD;
	tuners->cpu_up_avg_times = CPU_UP_AVG_TIMES;
	tuners->cpu_down_avg_times = CPU_DOWN_AVG_TIMES;
	tuners->thermal_dispatch_avg_times = THERMAL_DISPATCH_AVG_TIMES;

	tuners->sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR;
	tuners->ignore_nice_load = 0;
	tuners->powersave_bias = 0;
	tuners->io_is_busy = should_io_be_busy();

	tuners->cpu_num_limit = num_possible_cpus();
	tuners->cpu_num_base = 1;

	tuners->ambient_mode = 0;
	
	if (tuners->cpu_num_limit > 1)
		tuners->is_cpu_hotplug_disable = 0;

#if INPUT_BOOST
	 tuners->cpu_input_boost_enable = 1;
#endif

#ifdef CONFIG_SMP
	INIT_DEFERRABLE_WORK(&hp_work, hp_work_handler);
#endif

#ifdef DEBUG_LOG
	pr_debug("cpufreq_gov_dbs_init: min_sampling_rate = %d\n", dbs_data->min_sampling_rate);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.up_threshold = %d\n", tuners->up_threshold);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.od_threshold = %d\n", tuners->od_threshold);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.down_differential = %d\n", tuners->down_differential);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_up_threshold = %d\n", tuners->cpu_up_threshold);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_down_differential = %d\n", tuners->cpu_down_differential);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_up_avg_times = %d\n", tuners->cpu_up_avg_times);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_down_avg_times = %d\n", tuners->cpu_down_avg_times);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.thermal_di_avg_times = %d\n", tuners->thermal_dispatch_avg_times);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_num_limit = %d\n", tuners->cpu_num_limit);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_num_base = %d\n", tuners->cpu_num_base);
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.is_cpu_hotplug_disable = %d\n", tuners->is_cpu_hotplug_disable);
#if INPUT_BOOST
	pr_debug("cpufreq_gov_dbs_init: dbs_tuners_ins.cpu_input_boost_enable = %d\n", tuners->cpu_input_boost_enable);
#endif /* INPUT_BOOST */
#endif /* DEBUG_LOG */
/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */

	dbs_data->tuners = tuners;
	mutex_init(&dbs_data->mutex);
	return 0;
}

static void bl_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

define_get_cpu_dbs_routines(bl_cpu_dbs_info);

static struct bl_ops bl_ops = {
	.powersave_bias_init_cpu = balance_powersave_bias_init_cpu,
	.powersave_bias_target = generic_powersave_bias_target,
	.freq_increase = dbs_freq_increase,
	.input_handler = &dbs_input_handler,
};

static struct common_dbs_data bl_dbs_cdata = {
	.governor = GOV_BALANCE,
	.attr_group_gov_sys = &bl_attr_group_gov_sys,
	.attr_group_gov_pol = &bl_attr_group_gov_pol,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = bl_dbs_timer,
	.gov_check_cpu = bl_check_cpu,
	.gov_ops = &bl_ops,
	.init = bl_init,
	.exit = bl_exit,
};

static void bl_set_powersave_bias(unsigned int powersave_bias)
{
	struct cpufreq_policy *policy;
	struct dbs_data *dbs_data;
	struct bl_dbs_tuners *bl_tuners;
	unsigned int cpu;
	cpumask_t done;

	default_powersave_bias = powersave_bias;
	cpumask_clear(&done);

	get_online_cpus();
	for_each_online_cpu(cpu) {
		if (cpumask_test_cpu(cpu, &done))
			continue;

		policy = per_cpu(bl_cpu_dbs_info, cpu).cdbs.cur_policy;
		if (!policy)
			continue;

		cpumask_or(&done, &done, policy->cpus);

		if (policy->governor != &cpufreq_gov_balance)
			continue;

		dbs_data = policy->governor_data;
		bl_tuners = dbs_data->tuners;
		bl_tuners->powersave_bias = default_powersave_bias;
	}
	put_online_cpus();
}

void bl_register_powersave_bias_handler(unsigned int (*f)
		(struct cpufreq_policy *, unsigned int, unsigned int),
		unsigned int powersave_bias)
{
	bl_ops.powersave_bias_target = f;
	bl_set_powersave_bias(powersave_bias);
}
EXPORT_SYMBOL_GPL(bl_register_powersave_bias_handler);

void bl_unregister_powersave_bias_handler(void)
{
	bl_ops.powersave_bias_target = generic_powersave_bias_target;
	bl_set_powersave_bias(0);
}
EXPORT_SYMBOL_GPL(bl_unregister_powersave_bias_handler);

static int bl_idle_notifier(struct notifier_block *nb,
					     unsigned long val,
					     void *data)
{
	if (atomic_read(&hotplug_cpu_count) != 1)
		return 0;

	switch (val) {
	case IDLE_START:
		bl_enable_timer(0);
		break;
	case IDLE_END:
		bl_enable_timer(1);
		break;
	}

	return 0;
}

struct notifier_block bl_idle_nb = {
	.notifier_call = bl_idle_notifier,
};

static int bl_cpufreq_governor_dbs(struct cpufreq_policy *policy,
		unsigned int event)
{
	#if 0
	// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
		struct dbs_data *dbs_data;
		int rc = 0;
	
		if (have_governor_per_policy())
			dbs_data = policy->governor_data;
		else
			dbs_data = hp_dbs_cdata.gdbs_data;
	
		//pr_emerg("***** policy->cpu: %d, event: %u, smp_processor_id: %d, have_governor_per_policy: %d *****\n", policy->cpu, event, smp_processor_id(), have_governor_per_policy());
		switch (event) {
		case CPUFREQ_GOV_START:
			#ifdef DEBUG_LOG
			{
				struct hp_dbs_tuners *hp_tuners = dbs_data->tuners;
	
				BUG_ON(NULL == dbs_data);
				BUG_ON(NULL == dbs_data->tuners);
	
				pr_debug("cpufreq_governor_dbs: min_sampling_rate = %d\n", dbs_data->min_sampling_rate);
				pr_debug("cpufreq_governor_dbs: dbs_tuners_ins.sampling_rate = %d\n", hp_tuners->sampling_rate);
				pr_debug("cpufreq_governor_dbs: dbs_tuners_ins.io_is_busy = %d\n", hp_tuners->io_is_busy);
			}
			#endif

			break;
	
		case CPUFREQ_GOV_STOP:
			break;
		}
	
	// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	#endif
	
	return cpufreq_governor_dbs(policy, &bl_dbs_cdata, event);
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_BALANCE
static
#endif
struct cpufreq_governor cpufreq_gov_balance = {
	.name			= "balance",
	.governor		= bl_cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	#if INPUT_BOOST
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	freq_up_task = kthread_create(touch_freq_up_task, NULL, "touch_freq_up_task");

	if (IS_ERR(freq_up_task))
		return PTR_ERR(freq_up_task);

	sched_setscheduler_nocheck(freq_up_task, SCHED_FIFO, &param);
	get_task_struct(freq_up_task);
	#endif
	
	return cpufreq_register_governor(&cpufreq_gov_balance);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	#ifdef CONFIG_SMP
	cancel_delayed_work_sync(&hp_work);
	#endif

	cpufreq_unregister_governor(&cpufreq_gov_balance);

	#if INPUT_BOOST
	kthread_stop(freq_up_task);
	put_task_struct(freq_up_task);
	#endif
}

MODULE_AUTHOR("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_AUTHOR("Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>");
MODULE_DESCRIPTION("'cpufreq_balance' - A dynamic cpufreq governor for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_BALANCE
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
