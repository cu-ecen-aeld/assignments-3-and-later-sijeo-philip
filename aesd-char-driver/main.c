/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Sijeo Philip"); /**TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    PDEBUG("open\n");
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release\n");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    size_t entry_offset_byte_rtn;
    struct aesd_buffer_entry *entry;
    
    if( mutex_lock_interruptible(&dev->lock))
    	return -ERESTARTSYS;
    	
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, *f_pos, &entry_offset_byte_rtn);
    
    if( entry ) {
    	size_t bytes_available = entry->size - entry_offset_byte_rtn;
    	size_t to_copy = min(count, bytes_available);
    	if( copy_to_user( buf, entry->buffptr + entry_offset_byte_rtn, to_copy)) {
    		retval = -EFAULT;
    	}else {
    		*f_pos += to_copy;
    		retval = to_copy;
    	} 
    } else {
    	retval = 0;
    }
    
    mutex_unlock(&dev->lock);
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *new_buf;
    size_t new_size;
    
    if( mutex_lock_interruptible(&dev->lock))
    	return -ERESTARTSYS;
    	
    /* Accumulate Incoming data */
    new_size = dev->partial_write_len + count;
    new_buf = kmalloc(new_size, GFP_KERNEL);
    if( !new_buf ) {
    	mutex_unlock(&dev->lock);
    	return -ENOMEM;
   }
   if( dev->partial_write_buf )
   {
   	memcpy(new_buf, dev->partial_write_buf, dev->partial_write_len);
   	kfree(dev->partial_write_buf);
   }
   if( copy_from_user(new_buf + dev->partial_write_len, buf, count))
   {
   kfree(new_buf);
   mutex_unlock(&dev->lock);
   return -EFAULT;
   }
   
   /*Check new line termination */
   char *nl = memchr(new_buf, '\n', new_size);
   if( nl ){
   	size_t cmd_size = nl - new_buf + 1;
   	struct aesd_buffer_entry entry;
   	
   	/* Free overwritten entry if buffer is full */
   	if ( dev->circ_buf.full ){
   		uint8_t idx = dev->circ_buf.in_offs;
   		kfree((char*)dev->circ_buf.entry[idx].buffptr);
   	}
   	
   	/*Prepare new entry*/
   	entry.buffptr = kmalloc(cmd_size, GFP_KERNEL);
   	if( !entry.buffptr ){
   		kfree(new_buf);
   		mutex_unlock(&dev->lock);
   		return -ENOMEM;
   	}
   	entry.size = cmd_size;
   	memcpy((char*)entry.buffptr, new_buf, cmd_size);
   	aesd_circular_buffer_add_entry(&dev->circ_buf, &entry);
   	
   	/* Save leftover for next partial */
   	size_t leftover = new_size - cmd_size;
   	if( leftover ) {
   		char *left_buf = kmalloc(leftover, GFP_KERNEL);
   		if (left_buf) {
   			memcpy(left_buf, new_buf + cmd_size, leftover);
   			dev->partial_write_buf = left_buf;
   			dev->partial_write_len = leftover;
   		}else {
   			dev->partial_write_buf = NULL;
   			dev->partial_write_len = 0;
   		}
   	}else {
   		dev->partial_write_buf = NULL;
   		dev->partial_write_len = 0;
   	}
   	kfree(new_buf);
   }else {
   	/*No newline yet, keep partial */
   	dev->partial_write_buf = new_buf;
   	dev->partial_write_len = new_size;
   }
   retval = count;
   mutex_unlock(&dev->lock);
   PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
   return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /*Initialize device data */
    aesd_circular_buffer_init(&aesd_device.circ_buf);
    mutex_init(&aesd_device.lock);
    aesd_device.partial_write_buf = NULL;
    aesd_device.partial_write_len = 0;
	
    /* Register char device */
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index = 0;
    struct aesd_buffer_entry *entry;
    cdev_del(&aesd_device.cdev);

    /*Free all stored enteries*/
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circ_buf, index);
    kfree((char*)entry->buffptr);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
