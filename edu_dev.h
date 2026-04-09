#ifndef EDU_DEV_H
#define EDU_DEV_H

#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/wait.h>

struct edu_dev {
    struct pci_dev  *pdev;
    void __iomem    *mmio_base;
    struct cdev     cdev;
    dev_t           dev_num;
    uint32_t        last_result;
    bool            irq_done;
    wait_queue_head_t wq;
};

#endif /* EDU_DEV_H */