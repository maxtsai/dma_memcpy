
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/freezer.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/miscdevice.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>


#define BUF_SIZE  1920 * 1080 * 4

static struct mutex lock;
static bool dma_done = false;

static dma_addr_t src_phy, dst_phy;
static char __iomem *src_va, *dst_va;
static bool src_mmapped = false, dst_mmapped = false;
static struct cdev dma_cdev;
static struct class *dma_class;
static int major_r;

static char *test_buf;

static DECLARE_WAIT_QUEUE_HEAD(wq);



static void callback(void *arg)
{
	//pr_info("tx done. notified by interrupt");
	dma_done = true;
	wake_up_interruptible(&wq);
}

static int dmacpy_fun(void)
{
	struct dma_chan *chan;
	dma_cap_mask_t mask;

	struct dma_async_tx_descriptor *txd;
	dma_cookie_t cookie;

	unsigned long long start_time, end_time;
	struct timespec64 tv;

	/*
	memset(src_va, 0xAA, BUF_SIZE);
	memset(dst_va, 0x55, BUF_SIZE);
	*/

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	chan = dma_request_channel(mask, NULL, NULL);
	if (!chan) {
		pr_err("No dma channel ready\n");
		return -EBUSY;
	}

	ktime_get_real_ts64(&tv);
	start_time = (tv.tv_sec * 1000000ULL) + (tv.tv_nsec / 1000); // in microseconds

	txd = dmaengine_prep_dma_memcpy(chan, dst_phy, src_phy, BUF_SIZE, DMA_PREP_INTERRUPT|DMA_CTRL_ACK);
	if (!txd) {
		pr_err("Error preparing a DMA transaction descriptoor");
		return -EIO;
	}
	txd->callback = callback;

	cookie = dmaengine_submit(txd);
	if (dma_submit_error(cookie)) {
		pr_err("Fail to do DMA tx_submit");
		return -EIO;
	}
	dma_async_issue_pending(chan);

	wait_event_interruptible(wq, dma_done);

	dma_release_channel(chan);

	ktime_get_real_ts64(&tv);
	end_time = (tv.tv_sec * 1000000ULL) + (tv.tv_nsec / 1000); // in microseconds

	if (memcmp(src_va, dst_va, BUF_SIZE) == 0)
		printk("verify ok. consume %lld us.\n", end_time-start_time);
	else
		printk("verify fail\n");
	return 0;
}
static int dmacpy_run_set(const char *val, const struct kernel_param *kp)
{
	mutex_lock(&lock);
	dmacpy_fun();
	mutex_unlock(&lock);
	return 0;
}
static int dmacpy_run_get(char *val, const struct kernel_param *kp)
{
	return param_get_bool(val, kp);
}
static const struct kernel_param_ops run_ops = {
	.set = dmacpy_run_set,
	.get = dmacpy_run_get,
};
static bool dmatest_run;
module_param_cb(dmacpy, &run_ops, &dmatest_run, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dmacpy, "run a test");

static int dmacpy_show(char *val, const struct kernel_param *kp)
{
	int i;
	for (i = 0; i < BUF_SIZE; i += (BUF_SIZE/10))
#if 1
		printk("[%d] [0x%x : 0x%x]\n", i, src_va[i], dst_va[i]);
#else
		printk("[%d] [0x%x]\n", i, test_buf[i]);
#endif
	return 0;
}
static const struct kernel_param_ops show_ops = {
	.set = NULL,
	.get = dmacpy_show,
};
module_param_cb(show, &show_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(show, "show buf");


static int dmacpy_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	int ret = 0;

	if (size > BUF_SIZE)
		return -EINVAL;
#if 1
	if (src_mmapped && dst_mmapped)
		return -ENOMEM;

	//vma->vm_flags |= VM_LOCKED;
	//vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	if (!src_mmapped) {
		vma->vm_pgoff = src_phy >> PAGE_SHIFT;
		ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       size, vma->vm_page_prot);
		if (ret == 0)
			src_mmapped = true;
	} else if (!dst_mmapped) {
		vma->vm_pgoff = dst_phy >> PAGE_SHIFT;
		ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       size, vma->vm_page_prot);
		if (ret == 0)
			dst_mmapped = true;
	}

	if (ret)
		ret = -ENOBUFS;
	printk("map size %ld, (src, dst) = (%d, %d)\n", size, src_mmapped, dst_mmapped);
#else
	pr_info("%s:%d\n", __func__, __LINE__);
	vma->vm_pgoff = virt_to_phys(test_buf) >> PAGE_SHIFT;
	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
		       size, vma->vm_page_prot);
	if (ret)
		return -ENOBUFS;
#endif

	return ret;
}

static int dmacpy_release(struct inode *inode, struct file *file)
{
	mutex_lock(&lock);
	src_mmapped = false;
	dst_mmapped = false;
	mutex_unlock(&lock);

	return 0;
}

static const struct file_operations fops =
{
	.owner = THIS_MODULE,
	.mmap = dmacpy_mmap,
	.release = dmacpy_release,
};


static int dmacpy_probe(struct platform_device *pdev)
{
	struct device *dma_device;
	dev_t devno = 0;
	int ret;

	/*** allocate buffers ***/
	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "No memory reserved\n");
		return -ENODEV;
	}
	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)))
	{
		dev_err(&pdev->dev, "Fail to set dma mask and coherent\n");
		return -ENODEV;
	}
	src_va = dma_alloc_coherent(&pdev->dev, BUF_SIZE, &src_phy, GFP_DMA);
	if (src_va == NULL) {
		dev_err(&pdev->dev, "unable to allocate src buffer\n");
		return -ENOMEM;
	}
	dst_va = dma_alloc_coherent(&pdev->dev, BUF_SIZE, &dst_phy, GFP_DMA);
	if (dst_va == NULL) {
		dev_err(&pdev->dev, "unable to allocate dst buffer\n");
		dma_free_coherent(&pdev->dev, BUF_SIZE, src_va, src_phy);
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "allocate %d bytes for src %p : %lld, and dst %p : %lld\n",
			BUF_SIZE, src_va, src_phy, dst_va, dst_phy);

	/*** create char device for file operations ***/
	dma_class = class_create(THIS_MODULE, "dmacpy");
	alloc_chrdev_region(&devno, 0, 1, "dmacpy");
	major_r = MAJOR(devno);
	cdev_init(&dma_cdev, &fops);
	cdev_add(&dma_cdev, devno, 1);
	dma_device = device_create(dma_class, NULL, MKDEV(major_r, 0), NULL, "dmacpy");

	test_buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	memset(test_buf, 0x55, BUF_SIZE);

	return 0;
}
static int dmacpy_remove(struct platform_device *pdev)
{
	dev_t devno = MKDEV(major_r, 0);

	if (src_va)
		dma_free_coherent(&pdev->dev, BUF_SIZE, src_va, src_phy);
	if (dst_va)
		dma_free_coherent(&pdev->dev, BUF_SIZE, dst_va, dst_phy);

	device_destroy(dma_class, MKDEV(major_r, 0));
	class_destroy(dma_class);
	cdev_del(&dma_cdev);
	unregister_chrdev_region(devno, 1);

	if (test_buf)
		kfree(test_buf);

	return 0;
}


static const struct of_device_id dmacpy_of_table[] = {
	{.compatible = "dmacpy,reserved-memory"},
	{}
};
MODULE_DEVICE_TABLE(of, dmacpy_of_table);
static struct platform_driver dmacpy_platform_driver = {
	.probe = dmacpy_probe,
	.remove = dmacpy_remove,
	.driver = {
		   .name = "dmacpy",
		   .of_match_table = dmacpy_of_table,
		   },
};

module_platform_driver(dmacpy_platform_driver);

MODULE_LICENSE("GPL v2");
