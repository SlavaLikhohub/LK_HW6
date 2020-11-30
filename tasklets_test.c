// SPDX-License-Identifier: GPL-2.0

/*
 * TASK:
 * Використовуючи таймер високої роздільної здатності, напишіть модуль,
 * який демонструє запуск і виконання низькопріоритетного та високопріоритетного
 * тасклетів. Упевніться, що високопріоритетні тасклети виконуються раніше,
 * навіть якщо в межах одного виклику функції таймера заплановані пізніше за
 * низькопріоритетні.

 * З кожного з тасклетів попереднього кроку заплануйте виконання work та
 * delayed work. Можна використати системні черги, хоча у прикладах
 * є як створити свою.
 *
 * При вивантаженні модуля не забудьте скасувати заплановані work-и.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/delay.h>

MODULE_AUTHOR("Viaheslav Lykhohub <viacheslav.lykhohub@globallogic.com>");
MODULE_DESCRIPTION("Test of tasklets");
MODULE_LICENSE("GPL");

#define MS_TO_NS(x)     ((x) * NSEC_PER_MSEC)

#define SCHEDULE_TLET(data) 					\
	do { 							\
		if ((data).hi) 					\
			tasklet_hi_schedule(&(data).tlet); 	\
		else 						\
			tasklet_schedule(&(data).tlet); 	\
	} while (0)

#define RESET_TLET_LOOPS(data) 				\
	do { 						\
		(data).loops = (data).loops_init; 	\
	} while (0)

#define MS_TO_JIFFIES(ms) ((ms) * HZ / 1000)

/* High resolution timer */
static struct hrtimer hr_timer;

static __s32 restart = 2;
static __u64 delay_ms = 1000L;

/* Tasklets */
struct tasklet_data {
	char msg[10];
	const __s32 loops_init;
	__s32 loops;
	__u8 hi;
	struct tasklet_struct tlet;
};

static struct tasklet_data tlet_data_hi = {
	.msg = "Hello",
	.loops_init = 2,
	.hi = 1
};

static struct tasklet_data tlet_data_low = {
	.msg = "Bye",
	.loops_init = 2,
	.hi = 0
};

/* Workqueue */
struct work_data_common {
	char msg[32];
	__u32 execution_time;
};

struct work_data {
	struct work_data_common com;
	struct work_struct work;
};

struct delayed_work_data {
	struct work_data_common com;
	struct delayed_work work;
	__u32 delay_ms;
};

static void work_func(struct work_struct *work_arg);

static struct workqueue_struct *wq_test;

static struct work_data work1_data __attribute__((used)) = {
	.com.msg = "Work 1",
	.com.execution_time = 200
};

static struct delayed_work_data work2_data __attribute__((used)) = {
	.com.msg = "Work2 (delayed)",
	.com.execution_time = 300,
	.delay_ms = 100
};

static enum hrtimer_restart hr_timer_callback(struct hrtimer *timer)
{
	pr_debug("hr_timer_callback called (%llu).\n",
			ktime_to_ms(timer->base->get_time()));

	RESET_TLET_LOOPS(tlet_data_low);
	RESET_TLET_LOOPS(tlet_data_hi);
	SCHEDULE_TLET(tlet_data_low);
	SCHEDULE_TLET(tlet_data_hi);

	if (likely(--restart > 0)) {
		hrtimer_forward_now(timer, ns_to_ktime(MS_TO_NS(delay_ms)));
		return HRTIMER_RESTART;
	}

	return HRTIMER_NORESTART;
}

static void tlet_func(unsigned long arg)
{
	struct tasklet_data *data = (typeof(data)) arg;

	pr_debug("tlet_func called (%lu).\n", jiffies);
	pr_info("Tasklet's saying \"%s\" (loop: %d)\n",
			data->msg,
			data->loops_init - data->loops);

	queue_work(wq_test, &work1_data.work);
	queue_delayed_work(	wq_test,
				&work2_data.work,
				MS_TO_JIFFIES(work2_data.delay_ms));

	if (likely(--data->loops > 0)) {
		SCHEDULE_TLET(*data);
	}
}

static void work_func(struct work_struct *work_arg)
{
	struct work_data *data;
	data = container_of(work_arg, typeof(*data), work);

	pr_info("Starting work: %s\n", data->com.msg);
	pr_debug("Delay: %d, Time: %lu\n", data->com.execution_time, jiffies);

	msleep(data->com.execution_time);

	pr_info("Finishing work: %s\n", data->com.msg);
	pr_debug("Time: %lu\n", jiffies);
}

static int __init tasklets_test_init(void)
{
	ktime_t ktime;
	pr_debug("Initializing tasklets\n");

	tasklet_init(	&tlet_data_hi.tlet,
			tlet_func,
			(unsigned long)&tlet_data_hi);

	tasklet_init(	&tlet_data_low.tlet,
			tlet_func,
			(unsigned long)&tlet_data_low);

	pr_debug("Initializing workqueue\n");

	wq_test = alloc_workqueue("Test wq", 0, 4);
	INIT_WORK(&work1_data.work, work_func);
	INIT_DELAYED_WORK(&work2_data.work, work_func);

	pr_debug("Staring HR Timer module installation\n");

	ktime = ktime_set(0, MS_TO_NS(delay_ms));
	hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr_timer.function = &hr_timer_callback;

	pr_debug("Starting timer to fire in %llu ms (%lu)\n",
		ktime_to_ms(hr_timer.base->get_time()) + delay_ms, jiffies);

	hrtimer_start(&hr_timer, ktime, HRTIMER_MODE_REL);

	return 0;
}

static void __exit tasklets_test_exit(void)
{
	int ret;
	ret = hrtimer_cancel(&hr_timer);

	if (ret)
		pr_info("The timer still in use\n");
	pr_info("HR Timer module has been uninstalled\n");

	cancel_work_sync(&work1_data.work);
	cancel_delayed_work_sync(&work2_data.work);
	flush_workqueue(wq_test);
	destroy_workqueue(wq_test);

	pr_info("Works have been canceled\n");
}

module_init(tasklets_test_init);
module_exit(tasklets_test_exit);
