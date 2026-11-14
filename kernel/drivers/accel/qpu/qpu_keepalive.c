// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * Dynamic Decoupling Engine (q-keepalived)
 * Automatically injects CPMG and XY-8 refocusing pulse trains during idle intervals.
 *
 * Copyright (C) 2026 OmniQ Project.
 */

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

	pr_info("omniq_qpu: q-keepalived dynamic decoupling thread started\n");

	while (!kthread_should_stop()) {
		/*
		 * In production hardware:
		 * Scan partition queues for idle qubits awaiting synchronization.
		 * If idle time > 2 us, issue XY-8 refocusing pulse sequence
		 * (X - Y - X - Y - Y - X - Y - X) to counteract 1/f environmental noise.
		 */
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
