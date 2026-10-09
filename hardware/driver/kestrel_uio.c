#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define DRIVER_NAME "kestrel_uio"
#define CLASS_NAME  "kestrel"
#define KESTREL_PCI_VENDOR_ID 0x10EE
#define KESTREL_PCI_DEVICE_ID 0x9038

#define KESTREL_IOCTL_MAGIC 0x8E
#define KESTREL_IOCTL_ALLOC_RING      0x8E01
#define KESTREL_IOCTL_GET_STATS       0x8E02
#define KESTREL_IOCTL_RESET_STATS     0x8E03

#define KESTREL_RING_ENTRY_COUNT 262144

#define KESTREL_REG_CTRL         0x00
#define KESTREL_REG_STATUS       0x04
#define KESTREL_REG_RING_BASE_LO 0x08
#define KESTREL_REG_RING_BASE_HI 0x0C
#define KESTREL_REG_RING_SIZE    0x10
#define KESTREL_REG_HEAD_PTR     0x14
#define KESTREL_REG_TAIL_PTR     0x18

struct dma_order_descriptor {
    uint64_t order_id;
    uint32_t shares;
    uint32_t price;
    uint16_t stock_locate;
    uint8_t  side;
    uint8_t  msg_type;
    uint64_t timestamp_ns;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed, aligned(32)));

struct kestrel_user_ring {
    uint32_t head ____cacheline_aligned;
    uint32_t tail ____cacheline_aligned;
    struct dma_order_descriptor descriptors[KESTREL_RING_ENTRY_COUNT];
} __attribute__((packed));

struct kestrel_driver_stats {
    uint64_t total_rx_packets;
    uint64_t total_orders_parsed;
    uint64_t total_ring_overflows;
    uint64_t dma_bus_errors;
};

struct kestrel_device {
    struct pci_dev *pdev;
    void __iomem *bar0_addr;
    resource_size_t bar0_len;

    void *ring_cpu_addr;
    dma_addr_t ring_dma_handle;
    size_t ring_total_bytes;

    struct cdev cdev;
    dev_t dev_num;
    struct class *dev_class;
    struct device *device;

    struct kestrel_driver_stats stats;
    spinlock_t lock;
};

static struct kestrel_device *g_kdev = NULL;

static int kestrel_open(struct inode *inodep, struct file *filep) {
    filep->private_data = g_kdev;
    return 0;
}

static int kestrel_release(struct inode *inodep, struct file *filep) {
    return 0;
}

static int kestrel_mmap(struct file *filep, struct vm_area_struct *vma) {
    struct kestrel_device *kdev = filep->private_data;
    unsigned long pfn;
    int ret;

    if (!kdev || !kdev->ring_cpu_addr) return -EINVAL;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    pfn = dma_to_phys(&kdev->pdev->dev, kdev->ring_dma_handle) >> PAGE_SHIFT;

    ret = remap_pfn_range(vma, vma->vm_start, pfn, vma->vm_end - vma->vm_start, vma->vm_page_prot);
    if (ret) return -EAGAIN;

    return 0;
}

static long kestrel_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
    struct kestrel_device *kdev = filep->private_data;
    if (!kdev) return -ENODEV;

    switch (cmd) {
        case KESTREL_IOCTL_GET_STATS: {
            if (copy_to_user((void __user *)arg, &kdev->stats, sizeof(kdev->stats))) {
                return -EFAULT;
            }
            break;
        }
        case KESTREL_IOCTL_RESET_STATS: {
            spin_lock(&kdev->lock);
            memset(&kdev->stats, 0, sizeof(kdev->stats));
            spin_unlock(&kdev->lock);
            break;
        }
        default:
            return -ENOTTY;
    }
    return 0;
}

static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = kestrel_open,
    .release        = kestrel_release,
    .mmap           = kestrel_mmap,
    .unlocked_ioctl = kestrel_ioctl,
};

static int kestrel_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    int ret;
    struct kestrel_device *kdev;

    kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
    if (!kdev) return -ENOMEM;

    kdev->pdev = pdev;
    spin_lock_init(&kdev->lock);
    pci_set_drvdata(pdev, kdev);
    g_kdev = kdev;

    ret = pci_enable_device(pdev);
    if (ret) goto err_free_kdev;

    pci_set_master(pdev);

    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (ret) goto err_disable_pci;

    ret = pci_request_regions(pdev, DRIVER_NAME);
    if (ret) goto err_disable_pci;

    kdev->bar0_len = pci_resource_len(pdev, 0);
    kdev->bar0_addr = pci_iomap(pdev, 0, kdev->bar0_len);
    if (!kdev->bar0_addr) {
        ret = -EIO;
        goto err_release_regions;
    }

    kdev->ring_total_bytes = PAGE_ALIGN(sizeof(struct kestrel_user_ring));
    kdev->ring_cpu_addr = dma_alloc_coherent(&pdev->dev, kdev->ring_total_bytes, &kdev->ring_dma_handle, GFP_KERNEL);
    if (!kdev->ring_cpu_addr) {
        ret = -ENOMEM;
        goto err_iounmap;
    }

    memset(kdev->ring_cpu_addr, 0, kdev->ring_total_bytes);

    iowrite32(lower_32_bits(kdev->ring_dma_handle), kdev->bar0_addr + KESTREL_REG_RING_BASE_LO);
    iowrite32(upper_32_bits(kdev->ring_dma_handle), kdev->bar0_addr + KESTREL_REG_RING_BASE_HI);
    iowrite32(KESTREL_RING_ENTRY_COUNT, kdev->bar0_addr + KESTREL_REG_RING_SIZE);
    iowrite32(1, kdev->bar0_addr + KESTREL_REG_CTRL);

    ret = alloc_chrdev_region(&kdev->dev_num, 0, 1, DRIVER_NAME);
    if (ret < 0) goto err_free_dma;

    cdev_init(&kdev->cdev, &fops);
    kdev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&kdev->cdev, kdev->dev_num, 1);
    if (ret < 0) goto err_unregister_chrdev;

    kdev->dev_class = class_create(CLASS_NAME);
    if (IS_ERR(kdev->dev_class)) {
        ret = PTR_ERR(kdev->dev_class);
        goto err_cdev_del;
    }

    kdev->device = device_create(kdev->dev_class, NULL, kdev->dev_num, NULL, DRIVER_NAME);
    if (IS_ERR(kdev->device)) {
        ret = PTR_ERR(kdev->device);
        goto err_class_destroy;
    }

    return 0;

err_class_destroy:
    class_destroy(kdev->dev_class);
err_cdev_del:
    cdev_del(&kdev->cdev);
err_unregister_chrdev:
    unregister_chrdev_region(kdev->dev_num, 1);
err_free_dma:
    dma_free_coherent(&pdev->dev, kdev->ring_total_bytes, kdev->ring_cpu_addr, kdev->ring_dma_handle);
err_iounmap:
    pci_iounmap(pdev, kdev->bar0_addr);
err_release_regions:
    pci_release_regions(pdev);
err_disable_pci:
    pci_disable_device(pdev);
err_free_kdev:
    kfree(kdev);
    g_kdev = NULL;
    return ret;
}

static void kestrel_pci_remove(struct pci_dev *pdev) {
    struct kestrel_device *kdev = pci_get_drvdata(pdev);
    if (!kdev) return;

    iowrite32(0, kdev->bar0_addr + KESTREL_REG_CTRL);

    device_destroy(kdev->dev_class, kdev->dev_num);
    class_destroy(kdev->dev_class);
    cdev_del(&kdev->cdev);
    unregister_chrdev_region(kdev->dev_num, 1);

    if (kdev->ring_cpu_addr) {
        dma_free_coherent(&pdev->dev, kdev->ring_total_bytes, kdev->ring_cpu_addr, kdev->ring_dma_handle);
    }

    if (kdev->bar0_addr) {
        pci_iounmap(pdev, kdev->bar0_addr);
    }

    pci_release_regions(pdev);
    pci_disable_device(pdev);
    kfree(kdev);
    g_kdev = NULL;
}

static const struct pci_device_id kestrel_pci_ids[] = {
    { PCI_DEVICE(KESTREL_PCI_VENDOR_ID, KESTREL_PCI_DEVICE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, kestrel_pci_ids);

static struct pci_driver kestrel_pci_driver = {
    .name     = DRIVER_NAME,
    .id_table = kestrel_pci_ids,
    .probe    = kestrel_pci_probe,
    .remove   = kestrel_pci_remove,
};

static int __init kestrel_driver_init(void) {
    return pci_register_driver(&kestrel_pci_driver);
}

static void __exit kestrel_driver_exit(void) {
    pci_unregister_driver(&kestrel_pci_driver);
}

module_init(kestrel_driver_init);
module_exit(kestrel_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kestrel Quant Systems");
MODULE_DESCRIPTION("Zero-Copy Kernel-Bypass PCIe DMA Order Book Ingestion Driver");
MODULE_VERSION("1.0.0");
