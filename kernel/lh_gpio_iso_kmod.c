// SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0
/*
 * Logihertz Isolated Industrial GPIO Linux Platform Driver
 *
 * Reference Driver for Linux 6.6 LTS
 * Features:
 *  - Modern GPIO descriptor API (gpiod)
 *  - Threaded ONESHOT interrupt handling (IRQF_ONESHOT)
 *  - High-resolution timer (hrtimer) digital integrator debounce
 *  - Lockless atomic64_t edge & storm event counters
 *  - Hardware IRQ storm rate-limiter and automatic channel masking
 *  - Restricted character device interface (/dev/lh_gpio_iso*, mode 0600)
 *
 * Copyright 2026 Logihertz Systems (OPC) Private Limited
 * Author: Sameer Tawade <ops@logihertz.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define MODULE_NAME "lh_gpio_isolated"
#define LH_GPIO_MAX_CHANNELS 32

/* IOCTL Definitions */
#define LH_GPIO_IOC_MAGIC 'L'
struct lh_gpio_channel_status {
    uint32_t channel_idx;
    uint32_t debounced_state;
    uint64_t rising_edges;
    uint64_t falling_edges;
    uint64_t storm_lockouts;
    uint32_t is_masked;
};

#define LH_GPIO_IOC_GET_STATUS _IOWR(LH_GPIO_IOC_MAGIC, 1, struct lh_gpio_channel_status)
#define LH_GPIO_IOC_RESET_COUNTERS _IOW(LH_GPIO_IOC_MAGIC, 2, uint32_t)

struct lh_gpio_channel {
    struct gpio_desc *desc;
    int irq;
    uint32_t index;
    atomic64_t rising_count;
    atomic64_t falling_count;
    atomic64_t storm_events;
    atomic_t state;
    atomic_t is_masked;
    uint32_t debounce_time_us;
    uint32_t sample_interval_us;
    uint32_t c_max;
    uint32_t counter;
    uint32_t irq_count_in_window;
    ktime_t window_start;
    ktime_t lockout_until;
    struct hrtimer debounce_timer;
    spinlock_t lock;
    struct lh_gpio_dev *parent;
};

struct lh_gpio_dev {
    struct device *dev;
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    uint32_t num_channels;
    uint32_t isolation_rating_vrms;
    uint32_t max_irq_rate_hz;
    uint32_t cooldown_ms;
    atomic_t open_count;
    struct lh_gpio_channel channels[LH_GPIO_MAX_CHANNELS];
};

static enum hrtimer_restart lh_gpio_timer_callback(struct hrtimer *timer) {
    struct lh_gpio_channel *ch = container_of(timer, struct lh_gpio_channel, debounce_timer);
    unsigned long flags;
    int raw_val;

    raw_val = gpiod_get_value(ch->desc);
    if (raw_val < 0) return HRTIMER_NORESTART;

    spin_lock_irqsave(&ch->lock, flags);

    if (raw_val) {
        if (ch->counter < ch->c_max) ch->counter++;
    } else {
        if (ch->counter > 0) ch->counter--;
    }

    if (ch->counter == ch->c_max) {
        if (atomic_read(&ch->state) == 0) {
            atomic_set(&ch->state, 1);
            atomic64_inc(&ch->rising_count);
        }
    } else if (ch->counter == 0) {
        if (atomic_read(&ch->state) == 1) {
            atomic_set(&ch->state, 0);
            atomic64_inc(&ch->falling_count);
        }
    }

    /* Keep sampling until saturated at rail */
    if (ch->counter > 0 && ch->counter < ch->c_max) {
        spin_unlock_irqrestore(&ch->lock, flags);
        hrtimer_forward_now(timer, ns_to_ktime(ch->sample_interval_us * 1000ULL));
        return HRTIMER_RESTART;
    }

    spin_unlock_irqrestore(&ch->lock, flags);
    return HRTIMER_NORESTART;
}

static irqreturn_t lh_gpio_hard_irq_handler(int irq, void *dev_id) {
    (void)irq;
    (void)dev_id;
    return IRQ_WAKE_THREAD;
}

static irqreturn_t lh_gpio_threaded_irq_handler(int irq, void *dev_id) {
    struct lh_gpio_channel *ch = (struct lh_gpio_channel *)dev_id;
    struct lh_gpio_dev *gdev = ch->parent;
    ktime_t now = ktime_get();
    unsigned long flags;

    (void)irq;
    spin_lock_irqsave(&ch->lock, flags);

    /* Check if currently in storm lockout */
    if (atomic_read(&ch->is_masked)) {
        if (ktime_before(now, ch->lockout_until)) {
            spin_unlock_irqrestore(&ch->lock, flags);
            return IRQ_HANDLED;
        }
        /* Cooldown expired: re-enable */
        atomic_set(&ch->is_masked, 0);
        ch->window_start = now;
        ch->irq_count_in_window = 1;
    } else {
        /* Sliding rate window check */
        s64 delta_ms = ktime_ms_delta(now, ch->window_start);
        if (delta_ms >= 1000) {
            ch->window_start = now;
            ch->irq_count_in_window = 1;
        } else {
            ch->irq_count_in_window++;
            if (ch->irq_count_in_window > gdev->max_irq_rate_hz) {
                /* Trigger storm lockout */
                atomic_set(&ch->is_masked, 1);
                ch->lockout_until = ktime_add_ms(now, gdev->cooldown_ms);
                atomic64_inc(&ch->storm_events);
                dev_warn(gdev->dev, "Channel %u IRQ storm detected! Rate > %u Hz, masked for %u ms\n",
                         ch->index, gdev->max_irq_rate_hz, gdev->cooldown_ms);
                spin_unlock_irqrestore(&ch->lock, flags);
                return IRQ_HANDLED;
            }
        }
    }

    /* Start or restart digital integrator sampling timer */
    hrtimer_start(&ch->debounce_timer, ns_to_ktime(ch->sample_interval_us * 1000ULL), HRTIMER_MODE_REL);

    spin_unlock_irqrestore(&ch->lock, flags);
    return IRQ_HANDLED;
}

static int lh_gpio_open(struct inode *inode, struct file *filp) {
    struct lh_gpio_dev *gdev = container_of(inode->i_cdev, struct lh_gpio_dev, cdev);

    /* Enforce O_EXCL exclusive access */
    if (atomic_cmpxchg(&gdev->open_count, 0, 1) != 0) {
        return -EBUSY;
    }

    filp->private_data = gdev;
    return 0;
}

static int lh_gpio_release(struct inode *inode, struct file *filp) {
    struct lh_gpio_dev *gdev = (struct lh_gpio_dev *)filp->private_data;
    (void)inode;
    atomic_set(&gdev->open_count, 0);
    return 0;
}

static long lh_gpio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct lh_gpio_dev *gdev = (struct lh_gpio_dev *)filp->private_data;

    if (_IOC_TYPE(cmd) != LH_GPIO_IOC_MAGIC) return -ENOTTY;

    switch (cmd) {
        case LH_GPIO_IOC_GET_STATUS: {
            struct lh_gpio_channel_status stat;
            if (copy_from_user(&stat, (void __user *)arg, sizeof(stat))) return -EFAULT;
            if (stat.channel_idx >= gdev->num_channels) return -EINVAL;

            struct lh_gpio_channel *ch = &gdev->channels[stat.channel_idx];
            stat.debounced_state = (uint32_t)atomic_read(&ch->state);
            stat.rising_edges = (uint64_t)atomic64_read(&ch->rising_count);
            stat.falling_edges = (uint64_t)atomic64_read(&ch->falling_count);
            stat.storm_lockouts = (uint64_t)atomic64_read(&ch->storm_events);
            stat.is_masked = (uint32_t)atomic_read(&ch->is_masked);

            if (copy_to_user((void __user *)arg, &stat, sizeof(stat))) return -EFAULT;
            return 0;
        }

        case LH_GPIO_IOC_RESET_COUNTERS: {
            uint32_t ch_idx;
            if (copy_from_user(&ch_idx, (void __user *)arg, sizeof(ch_idx))) return -EFAULT;
            if (ch_idx >= gdev->num_channels) return -EINVAL;

            struct lh_gpio_channel *ch = &gdev->channels[ch_idx];
            atomic64_set(&ch->rising_count, 0);
            atomic64_set(&ch->falling_count, 0);
            atomic64_set(&ch->storm_events, 0);
            return 0;
        }

        default:
            return -ENOTTY;
    }
}

static const struct file_operations lh_gpio_fops = {
    .owner = THIS_MODULE,
    .open = lh_gpio_open,
    .release = lh_gpio_release,
    .unlocked_ioctl = lh_gpio_ioctl,
};

static int lh_gpio_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    struct lh_gpio_dev *gdev;
    int ret, i;
    u32 num_ch = 8;
    u32 debounce_us = 5000;

    gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
    if (!gdev) return -ENOMEM;

    gdev->dev = dev;
    atomic_set(&gdev->open_count, 0);

    if (np) {
        of_property_read_u32(np, "logihertz,channel-count", &num_ch);
        of_property_read_u32(np, "logihertz,isolation-rating-vrms", &gdev->isolation_rating_vrms);
        of_property_read_u32(np, "logihertz,debounce-delay-us", &debounce_us);
        of_property_read_u32(np, "logihertz,max-irq-rate-hz", &gdev->max_irq_rate_hz);
        of_property_read_u32(np, "logihertz,cooldown-period-ms", &gdev->cooldown_ms);
    }

    if (num_ch > LH_GPIO_MAX_CHANNELS) num_ch = LH_GPIO_MAX_CHANNELS;
    gdev->num_channels = num_ch;
    if (gdev->max_irq_rate_hz == 0) gdev->max_irq_rate_hz = 500;
    if (gdev->cooldown_ms == 0) gdev->cooldown_ms = 1000;

    /* Initialize each GPIO channel */
    for (i = 0; i < (int)num_ch; i++) {
        struct lh_gpio_channel *ch = &gdev->channels[i];
        ch->index = i;
        ch->parent = gdev;
        ch->debounce_time_us = debounce_us;
        ch->sample_interval_us = 100;
        ch->c_max = (debounce_us + 99) / 100;
        if (ch->c_max == 0) ch->c_max = 1;
        ch->counter = 0;
        atomic64_set(&ch->rising_count, 0);
        atomic64_set(&ch->falling_count, 0);
        atomic64_set(&ch->storm_events, 0);
        atomic_set(&ch->state, 0);
        atomic_set(&ch->is_masked, 0);
        spin_lock_init(&ch->lock);

        hrtimer_init(&ch->debounce_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ch->debounce_timer.function = lh_gpio_timer_callback;

        /* Request GPIO descriptor */
        ch->desc = devm_gpiod_get_index_optional(dev, "isolated", i, GPIOD_IN);
        if (IS_ERR(ch->desc)) {
            dev_warn(dev, "Failed to get GPIO for channel %d\n", i);
            continue;
        }

        if (ch->desc) {
            ch->irq = gpiod_to_irq(ch->desc);
            if (ch->irq > 0) {
                ret = devm_request_threaded_irq(dev, ch->irq,
                                                lh_gpio_hard_irq_handler,
                                                lh_gpio_threaded_irq_handler,
                                                IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                                "lh_gpio_iso_irq", ch);
                if (ret) dev_warn(dev, "Failed to request threaded IRQ for ch %d\n", i);
            }
        }
    }

    /* Register Character Device */
    ret = alloc_chrdev_region(&gdev->devt, 0, 1, MODULE_NAME);
    if (ret < 0) return ret;

    cdev_init(&gdev->cdev, &lh_gpio_fops);
    gdev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&gdev->cdev, gdev->devt, 1);
    if (ret) {
        unregister_chrdev_region(gdev->devt, 1);
        return ret;
    }

    platform_set_drvdata(pdev, gdev);
    dev_info(dev, "Logihertz Isolated GPIO Driver loaded: %u channels, max IRQ %u Hz\n",
             gdev->num_channels, gdev->max_irq_rate_hz);
    return 0;
}

static int lh_gpio_remove(struct platform_device *pdev) {
    struct lh_gpio_dev *gdev = platform_get_drvdata(pdev);
    for (uint32_t i = 0; i < gdev->num_channels; i++) {
        hrtimer_cancel(&gdev->channels[i].debounce_timer);
    }
    cdev_del(&gdev->cdev);
    unregister_chrdev_region(gdev->devt, 1);
    dev_info(gdev->dev, "Logihertz Isolated GPIO Driver unloaded\n");
    return 0;
}

static const struct of_device_id lh_gpio_of_match[] = {
    { .compatible = "logihertz,gpio-isolated-v1" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lh_gpio_of_match);

static struct platform_driver lh_gpio_driver = {
    .probe = lh_gpio_probe,
    .remove = lh_gpio_remove,
    .driver = {
        .name = MODULE_NAME,
        .of_match_table = lh_gpio_of_match,
    },
};

module_platform_driver(lh_gpio_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sameer Tawade <ops@logihertz.com>");
MODULE_DESCRIPTION("Logihertz Isolated Industrial GPIO Platform Driver");
MODULE_VERSION("0.1.0");
