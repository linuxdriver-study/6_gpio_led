#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/ide.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#define LEDDEV_CNT      1
#define LEDDEV_NAME     "gpioled"

#define LED_ON          1
#define LED_OFF         0

static int led_open(struct inode *nd, struct file *file);
static ssize_t led_write(struct file *file,
                         const char __user *user,
                         size_t size,
                         loff_t *loff);
static int led_release(struct inode *nd, struct file *file);

struct gpioled_dev {
        dev_t devid;
        int major;
        int minor;
        struct cdev cdev;
        struct class *class;
        struct device *device;
        struct device_node *nd;
        int gpio_led;
};
struct gpioled_dev leddev;

static const struct file_operations led_ops = {
        .owner = THIS_MODULE,
        .open = led_open,
        .write = led_write,
        .release = led_release,
};

static void led_switch(u8 led_status)
{
        if (led_status == LED_ON)
                gpio_set_value(leddev.gpio_led, 0);
        else if(led_status == LED_OFF)
                gpio_set_value(leddev.gpio_led, 1);
}

static int led_open(struct inode *nd, struct file *file)
{
        file->private_data = &leddev;

        return 0;
}

static ssize_t led_write(struct file *file,
                         const char __user *user,
                         size_t size,
                         loff_t *loff)
{
        int ret = 0;
        unsigned char buf[1];
        struct gpioled_dev *dev = file->private_data;

        ret = copy_from_user(buf, user, 1);
        if (ret < 0) {
                printk("kernel write error!\n");
                ret = -EFAULT;
                goto error;
        }
        if((buf[0] != LED_OFF) && (buf[0] != LED_ON)) {
                ret = -EINVAL;
                goto error;
        }

        led_switch(buf[0]);

error:
        return 0;
}

static int led_release(struct inode *nd, struct file *file)
{
        file->private_data = NULL;
        return 0;
}

static int __init gpioled_init(void)
{
        int ret = 0;

        /* 1.注册设备号 */
        if (leddev.major) {
                leddev.devid = MKDEV(leddev.major, leddev.minor);
                ret = register_chrdev_region(leddev.devid, LEDDEV_CNT, LEDDEV_NAME);
        } else {
                ret = alloc_chrdev_region(&leddev.devid, 0, LEDDEV_CNT, LEDDEV_NAME);
                leddev.major = MAJOR(leddev.devid);
                leddev.minor = MINOR(leddev.devid);
        }
        if (ret < 0) {
                printk("chrdeev region error!\n");
                goto fail_region; 
        }
        printk("major: %d, minor: %d\n", leddev.major, leddev.minor);

        /* 2.注册字符设备 */
        leddev.cdev.owner = THIS_MODULE;
        cdev_init(&leddev.cdev, &led_ops);
        ret = cdev_add(&leddev.cdev, leddev.devid, LEDDEV_CNT);
        if (ret != 0) {
                printk("cdev_add error!\n");
                goto fail_cdevadd;
        }
        
        /* 3.创建类和设备 */
        leddev.class = class_create(THIS_MODULE, LEDDEV_NAME);
        if (IS_ERR(leddev.class)) {
                ret = PTR_ERR(leddev.class);
                goto fail_class;
        }
        leddev.device = device_create(leddev.class, NULL, leddev.devid,
                                      NULL, LEDDEV_NAME);
        if (IS_ERR(leddev.device)) {
                ret = PTR_ERR(leddev.device);
                goto fail_device;
        }

        leddev.nd = of_find_node_by_path("/gpioled");
        if (leddev.nd == NULL) {
                printk("of_find_node_by_path error!\n");
                ret = -EINVAL;
                goto fail_findnode;
        }
        leddev.gpio_led = of_get_named_gpio(leddev.nd, "led-gpios", 0);
        if (leddev.gpio_led < 0) {
                printk("of_get_named_gpio error!\n");
                ret = -EINVAL;
                goto fail_getnamed;
        }
        ret = gpio_request(leddev.gpio_led, "led-gpios");
        if (ret != 0) {
                printk("gpio_request error!\n");
                goto fail_request;
        }
        ret = gpio_direction_output(leddev.gpio_led, 1);
        if (ret != 0) {
                printk("gpio direction output error!\n");
                goto fail_dir;
        }
        gpio_set_value(leddev.gpio_led, 0);     /* 打开led */

        goto success;

fail_dir:
        gpio_free(leddev.gpio_led);
fail_request:
fail_getnamed:
fail_findnode:
        device_destroy(leddev.class, leddev.devid);
fail_device:
        class_destroy(leddev.class);
fail_class:
        cdev_del(&leddev.cdev);
fail_cdevadd:
        unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
fail_region:
success:
        return ret;
}

static void __exit gpioled_exit(void)
{
        gpio_set_value(leddev.gpio_led, 1);
        gpio_free(leddev.gpio_led);
        device_destroy(leddev.class, leddev.devid);
        class_destroy(leddev.class);
        cdev_del(&leddev.cdev);
        unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
}

module_init(gpioled_init);
module_exit(gpioled_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");
