#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/pci.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
MODULE_IMPORT_NS("DMA_BUF");
#else
MODULE_IMPORT_NS(DMA_BUF);
#endif
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#endif

#include "qpu_internal.h"

#ifdef __KERNEL__
static struct sg_table *omniq_dmabuf_map(struct dma_buf_attachment *attach,
					 enum dma_data_direction dir)
{
	struct qpu_device *qdev = attach->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	if (!qdev || !qdev->dma_ring_virt)
		return ERR_PTR(-ENODEV);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}

	sg_set_buf(sgt->sgl, qdev->dma_ring_virt, qdev->dma_ring_size);

	ret = dma_map_sgtable(attach->dev, sgt, dir, 0);
	if (ret) {
		sg_free_table(sgt);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

static void omniq_dmabuf_unmap(struct dma_buf_attachment *attach,
			       struct sg_table *sgt,
			       enum dma_data_direction dir)
{
	if (!sgt)
		return;

	dma_unmap_sgtable(attach->dev, sgt, dir, 0);
	sg_free_table(sgt);
	kfree(sgt);
}

static void omniq_dmabuf_release(struct dma_buf *dmabuf)
{

	(void)dmabuf;
}

static int omniq_dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct qpu_device *qdev = dmabuf->priv;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset_bytes = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long pfn;

	if (!qdev || !qdev->dma_ring_virt)
		return -ENODEV;

	if (size == 0 || size > qdev->dma_ring_size ||
	    offset_bytes > qdev->dma_ring_size ||
	    size > (qdev->dma_ring_size - offset_bytes))
		return -EINVAL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
#else
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
#endif

	pfn = virt_to_phys((char *)qdev->dma_ring_virt + offset_bytes) >> PAGE_SHIFT;
	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static const struct dma_buf_ops omniq_dmabuf_ops = {
	.map_dma_buf   = omniq_dmabuf_map,
	.unmap_dma_buf = omniq_dmabuf_unmap,
	.release       = omniq_dmabuf_release,
	.mmap          = omniq_dmabuf_mmap,
};

struct dma_buf *qpu_dma_export_ring(struct qpu_device *qdev)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (!qdev || !qdev->dma_ring_virt || qdev->dma_ring_size == 0)
		return ERR_PTR(-ENODEV);

	exp_info.ops = &omniq_dmabuf_ops;
	exp_info.size = qdev->dma_ring_size;
	exp_info.flags = O_RDWR | O_CLOEXEC;
	exp_info.priv = qdev;

	return dma_buf_export(&exp_info);
}
#endif

int qpu_dma_init(struct qpu_device *qdev)
{
	if (!qdev)
		return -22;

	if (qdev->dma_ring_virt)
		return 0;

#ifdef __KERNEL__
	if (qdev->pdev) {
		int ret = dma_set_mask_and_coherent(&qdev->pdev->dev, DMA_BIT_MASK(64));
		if (ret) {
			pr_err("omniq_qpu: Failed to set 64-bit DMA mask\n");
			return ret;
		}
		qdev->dma_ring_virt = dma_alloc_coherent(&qdev->pdev->dev,
							 QPU_DMA_RING_SIZE,
							 &qdev->dma_ring_phys,
							 GFP_KERNEL | __GFP_ZERO);
		if (!qdev->dma_ring_virt)
			return -ENOMEM;
		qdev->dma_is_coherent = true;
	} else {
		qdev->dma_ring_virt = alloc_pages_exact(QPU_DMA_RING_SIZE,
							GFP_KERNEL | __GFP_ZERO);
		if (!qdev->dma_ring_virt)
			return -ENOMEM;
		qdev->dma_ring_phys = (dma_addr_t)virt_to_phys(qdev->dma_ring_virt);
		qdev->dma_is_coherent = false;
	}
	qdev->dma_ring_size = QPU_DMA_RING_SIZE;
#else
	if (posix_memalign(&qdev->dma_ring_virt, 4096, QPU_DMA_RING_SIZE) != 0 ||
	    !qdev->dma_ring_virt) {
		qdev->dma_ring_virt = NULL;
		return -12;
	}
	memset(qdev->dma_ring_virt, 0, QPU_DMA_RING_SIZE);
	qdev->dma_ring_size = QPU_DMA_RING_SIZE;
#endif

	return 0;
}

void qpu_dma_cleanup(struct qpu_device *qdev)
{
	if (!qdev || !qdev->dma_ring_virt)
		return;

#ifdef __KERNEL__
	if (qdev->dma_is_coherent && qdev->pdev) {
		dma_free_coherent(&qdev->pdev->dev, qdev->dma_ring_size,
				  qdev->dma_ring_virt, qdev->dma_ring_phys);
	} else {
		free_pages_exact(qdev->dma_ring_virt, qdev->dma_ring_size);
	}
	qdev->dma_ring_phys = 0;
	qdev->dma_is_coherent = false;
#else
	free(qdev->dma_ring_virt);
#endif

	qdev->dma_ring_virt = NULL;
	qdev->dma_ring_size = 0;
}

int qpu_dma_benchmark_ring(struct qpu_device *qdev, __u64 bytes, __u32 *out_ns)
{
	volatile __u64 *ring;
	size_t span, n_words, step, idx;
	__u64 expected_xor = 0;
	__u64 readback_xor = 0;
	__u64 elapsed_ns = 1;
#ifdef __KERNEL__
	__u64 t_start, t_end;
#else
	struct timespec ts_start, ts_end;
	__u64 t0_ns, t1_ns;
#endif

	if (!qdev || !out_ns)
		return -22;

	if (!qdev->dma_ring_virt) {
		int rc = qpu_dma_init(qdev);
		if (rc != 0)
			return rc;
	}

	span = (bytes == 0) ? 4096U : (size_t)bytes;
	if (span > qdev->dma_ring_size)
		span = qdev->dma_ring_size;

	n_words = span / sizeof(__u64);
	if (n_words == 0)
		n_words = 1;

	step = (n_words > 8) ? 8U : 1U;
	ring = (volatile __u64 *)qdev->dma_ring_virt;

#ifdef __KERNEL__
	t_start = ktime_get_ns();
#else
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
#endif

	for (idx = 0; idx < n_words; idx += step) {
		__u64 frame = 0x5145435F53594E00ULL ^ ((__u64)idx * 0x9E3779B97F4A7C15ULL);
		ring[idx] = frame;
		expected_xor ^= frame;
	}

#ifdef __KERNEL__
	smp_mb();
#else
	__sync_synchronize();
#endif

	for (idx = 0; idx < n_words; idx += step) {
		readback_xor ^= ring[idx];
	}

#ifdef __KERNEL__
	t_end = ktime_get_ns();
	elapsed_ns = (t_end > t_start) ? (t_end - t_start) : 1ULL;
#else
	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	t0_ns = (__u64)ts_start.tv_sec * 1000000000ULL + (__u64)ts_start.tv_nsec;
	t1_ns = (__u64)ts_end.tv_sec * 1000000000ULL + (__u64)ts_end.tv_nsec;
	elapsed_ns = (t1_ns > t0_ns) ? (t1_ns - t0_ns) : 1ULL;
#endif

	if (readback_xor != expected_xor)
		return -5;

	if (elapsed_ns == 0)
		elapsed_ns = 1;
	if (elapsed_ns > 0xFFFFFFFFULL)
		elapsed_ns = 0xFFFFFFFFULL;

	qdev->last_dma_bench_ns = elapsed_ns;
	*out_ns = (__u32)elapsed_ns;
	return 0;
}
