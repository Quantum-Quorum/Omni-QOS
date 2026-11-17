#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#else
#include <stdio.h>
#include <unistd.h>
#endif

#include "qpu_internal.h"

#ifdef __KERNEL__
static int qpu_keepalive_worker(void *data)
{
	struct qpu_device *qdev = (struct qpu_device *)data;
	(void)qdev;

	pr_info("omniq_qpu: q-keepalived dynamic decoupling thread started\n");

	while (!kthread_should_stop()) {
		msleep_interruptible(100);
	}

	pr_info("omniq_qpu: q-keepalived dynamic decoupling thread stopped\n");
	return 0;
}
#endif

int qpu_keepalive_start(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	qdev->keepalive_task = kthread_run(qpu_keepalive_worker, qdev, "q-keepalived");
	if (IS_ERR(qdev->keepalive_task)) {
		pr_err("omniq_qpu: Failed to spawn q-keepalived kthread\n");
		return PTR_ERR(qdev->keepalive_task);
	}
#else
	(void)qdev;
#endif
	return 0;
}

void qpu_keepalive_stop(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	if (qdev->keepalive_task) {
		kthread_stop(qdev->keepalive_task);
		qdev->keepalive_task = NULL;
	}
#else
	(void)qdev;
#endif
}

