#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>       // Required for the GPIO functions
#include <linux/interrupt.h>  // Required for the IRQ code
#include <linux/kobject.h>    // Using kobjects for the sysfs bindings
#include <linux/time.h>       // Using the clock to measure time between button presses
#define  DEBOUNCE_TIME 200    ///< The default bounce time -- 200ms

#include "../spi.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Peck"); // Thanks to Derek Molloy!
MODULE_DESCRIPTION("Touch screen interrupt driver");
MODULE_VERSION("0.1");

static bool isRising = 0;                   ///< Rising edge is the default IRQ property
module_param(isRising, bool, S_IRUGO);      ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(isRising, " Rising edge = 1, Falling edge = 0 (default)");  ///< parameter description

static unsigned int gpioTouch = GPIO_SPI0_INTR;
module_param(gpioTouch, uint, S_IRUGO);    ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioTouch, " GPIO Touch number (default=GPIO_SPI0_INTR)");  ///< parameter description

static bool isDebug = 0;
module_param(isDebug, bool, S_IRUGO);
MODULE_PARM_DESC(isDebug, " debug = 1, no-debug = 0 (default)");

static char   gpioName[8] = "gpioXXX";      ///< Null terminated default string -- just in case
static int    irqNumber;                    ///< Used to share the IRQ number within this file
static int    numberPresses = 0;            ///< For information, store the number of button presses
static bool   isDebounce = 1;               ///< Use to store the debounce state (on by default)
static struct timespec ts_last, ts_current, ts_diff;  ///< timespecs from linux/time.h (has nano precision)

/// Function prototype for the custom IRQ handler function -- see below for the implementation
static irq_handler_t  tftgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

/** @brief A callback function to output the numberPresses variable
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer to which to write the number of presses
 *  @return return the total number of characters written to the buffer (excluding null)
 */
static ssize_t numberPresses_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", numberPresses);
}

/** @brief A callback function to read in the numberPresses variable
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer from which to read the number of presses (e.g., reset to 0).
 *  @param count the number characters in the buffer
 *  @return return should return the total number of characters used from the buffer
 */
static ssize_t numberPresses_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count){
    sscanf(buf, "%du", &numberPresses);
    return count;
}

/** @brief Displays the last time the button was pressed -- manually output the date (no localization) */
static ssize_t lastTime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
    return sprintf(buf, "%.2lu:%.2lu:%.2lu:%.9lu \n", (ts_last.tv_sec/3600)%24,
                   (ts_last.tv_sec/60) % 60, ts_last.tv_sec % 60, ts_last.tv_nsec );
}

/** @brief Display the time difference in the form secs.nanosecs to 9 places */
static ssize_t diffTime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
    return sprintf(buf, "%lu.%.9lu\n", ts_diff.tv_sec, ts_diff.tv_nsec);
}

/** @brief Displays if button debouncing is on or off */
static ssize_t isDebounce_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", isDebounce);
}

/** @brief Stores and sets the debounce state */
static ssize_t isDebounce_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
    unsigned int temp;
    sscanf(buf, "%du", &temp);                // use a temp varable for correct int->bool
    gpio_set_debounce(gpioTouch,0);
    isDebounce = temp;
    if(isDebounce) { gpio_set_debounce(gpioTouch, DEBOUNCE_TIME);
        printk(KERN_INFO "TFT Display: Debounce on\n");
    }
    else { gpio_set_debounce(gpioTouch, 0);  // set the debounce time to 0
        printk(KERN_INFO "TFT Display: Debounce off\n");
    }
    return count;
}

/**  Use these helper macros to define the name and access levels of the kobj_attributes
 *  The kobj_attribute has an attribute attr (name and mode), show and store function pointers
 *  The count variable is associated with the numberPresses variable and it is to be exposed
 *  with mode 0666 using the numberPresses_show and numberPresses_store functions above
 */
#undef VERIFY_OCTAL_PERMISSIONS
#define VERIFY_OCTAL_PERMISSIONS(perms) (perms)
static struct kobj_attribute count_attr = __ATTR(numberPresses, 0666, numberPresses_show, numberPresses_store);
static struct kobj_attribute debounce_attr = __ATTR(isDebounce, 0666, isDebounce_show, isDebounce_store);

/**  The __ATTR_RO macro defines a read-only attribute. There is no need to identify that the
 *  function is called _show, but it must be present. __ATTR_WO can be  used for a write-only
 *  attribute but only in Linux 3.11.x on.
 */
static struct kobj_attribute time_attr  = __ATTR_RO(lastTime);  ///< the last time pressed kobject attr
static struct kobj_attribute diff_attr  = __ATTR_RO(diffTime);  ///< the difference in time attr

/**  The tft_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *tft_attrs[] = {
    &count_attr.attr,                  ///< The number of button presses
    &time_attr.attr,                   ///< Time of the last button press in HH:MM:SS:NNNNNNNNN
    &diff_attr.attr,                   ///< The difference in time between the last two presses
    &debounce_attr.attr,               ///< Is the debounce state true or false
    NULL,
};

/**  The attribute group uses the attribute array and a name, which is exposed on sysfs -- in this
 *  case it is gpio115, which is automatically defined in the tftDisplay_init() function below
 *  using the custom kernel parameter that can be passed when the module is loaded.
 */
static struct attribute_group attr_group = {
    .name  = gpioName,                 ///< The name is generated in tftDisplay_init()
    .attrs = tft_attrs,                ///< The attributes array defined just above
};

static struct kobject *tft_kobj;

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init tftDisplay_init(void){
    int result = 0;
    unsigned long IRQflags = IRQF_TRIGGER_FALLING;      // The default is a falling-edge interrupt
    
    printk(KERN_INFO "TFT Disply: Initializing the Touch interrupt\n");
    sprintf(gpioName, "gpio%d", gpioTouch);           // Create the gpioXXX name for /sys/tft/gpioXXX
    
    // create the kobject sysfs entry at /sys/tft
    tft_kobj = kobject_create_and_add("tft", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
    if(!tft_kobj){
        printk(KERN_ALERT "TFT Display: failed to create kobject mapping\n");
        return -ENOMEM;
    }
    // add the attributes to /sys/tft/ -- for example, /sys/tft/gpio115/numberPresses
    result = sysfs_create_group(tft_kobj, &attr_group);
    if(result) {
        printk(KERN_ALERT "TFT Display: failed to create sysfs group\n");
        kobject_put(tft_kobj);                          // clean up -- remove the kobject sysfs entry
        return result;
    }
    getnstimeofday(&ts_last);                          // set the last time to be the current time
    ts_diff = timespec_sub(ts_last, ts_last);          // set the initial time difference to be 0
    
    // the bool argument prevents the direction from being changed
    gpio_request(gpioTouch, "sysfs");       // Set up the gpioTouch
    gpio_direction_input(gpioTouch);        // Set the button GPIO to be an input
    gpio_set_debounce(gpioTouch, DEBOUNCE_TIME); // Debounce the button with a delay of 200ms
    gpio_export(gpioTouch, false);          // Causes gpio115 to appear in /sys/class/gpio
    // the bool argument prevents the direction from being changed
    
    // Perform a quick test to see that the button is working as expected on LKM load
    printk(KERN_INFO "TFT Display: The touch state is currently: %d\n", gpio_get_value(gpioTouch));
    
    /// GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
    irqNumber = gpio_to_irq(gpioTouch);
    printk(KERN_INFO "TFT Display: The touch is mapped to IRQ: %d\n", irqNumber);
    
    if(isRising){                           // If the kernel parameter isRising=0 is supplied
        IRQflags = IRQF_TRIGGER_RISING;      // Set the interrupt to be on the falling edge
    }
    // This next call requests an interrupt line
    result = request_irq(irqNumber,             // The interrupt number requested
                         (irq_handler_t) tftgpio_irq_handler, // The pointer to the handler function below
                         IRQflags,              // Use the custom kernel param to set interrupt type
                         "tft_touch_handler",  // Used in /proc/interrupts to identify the owner
                         NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
    return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit tftDisplay_exit(void){
    printk(KERN_INFO "TFT Display: The touch was pressed %d times\n", numberPresses);
    kobject_put(tft_kobj);                   // clean up -- remove the kobject sysfs entry
    free_irq(irqNumber, NULL);               // Free the IRQ number, no *dev_id required in this case
    gpio_unexport(gpioTouch);               // Unexport the Button GPIO
    gpio_free(gpioTouch);                   // Free the Button GPIO
    printk(KERN_INFO "TFT Display: Goodbye from the touch driver!\n");
}

/** @brief The GPIO IRQ Handler function
 *  This function is a custom interrupt handler that is attached to the GPIO above. The same interrupt
 *  handler cannot be invoked concurrently as the interrupt line is masked out until the function is complete.
 *  This function is static as it should not be invoked directly from outside of this file.
 *  @param irq    the IRQ number that is associated with the GPIO -- useful for logging.
 *  @param dev_id the *dev_id that is provided -- can be used to identify which device caused the interrupt
 *  Not used in this example as NULL is passed.
 *  @param regs   h/w specific register values -- only really ever used for debugging.
 *  return returns IRQ_HANDLED if successful -- should return IRQ_NONE otherwise.
 */
static irq_handler_t tftgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
    getnstimeofday(&ts_current);         // Get the current time as ts_current
    ts_diff = timespec_sub(ts_current, ts_last);   // Determine the time difference between last 2 presses
    ts_last = ts_current;                // Store the current time as the last time ts_last
    if(isDebug) printk(KERN_INFO "TFT Display: The touch state is currently: %d\n", gpio_get_value(gpioTouch));
    numberPresses++;                     // Global counter, will be outputted when the module is unloaded
    return (irq_handler_t) IRQ_HANDLED;  // Announce that the IRQ has been handled correctly
}

// This next calls are  mandatory -- they identify the initialization function
// and the cleanup function (as above).
module_init(tftDisplay_init);
module_exit(tftDisplay_exit);
