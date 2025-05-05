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
	// Approach 1 based on "Device Driver File Operations" lecture video
	// Handles Multiple device opens
	struct aesd_dev *aesd_device;

	PDEBUG("open");

	aesd_device = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = aesd_device;

	// Approach 2, directly assign the global var device (a single device only)
	// does not seem to work
	// filp->private_data = &aesd_device;

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
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte_rtn;
    size_t bytes_to_copy;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    /*Lock the mutex to ensure safe access to the circular buffer */
    if ( mutex_lock_interruptible(&dev->lock) )
	    return -ERESTARTSYS;

    /* Find the entry corresponding to the current file position */
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, *f_pos, &entry_offset_byte_rtn);
    if( !entry ){
	    PDEBUG("No entry found for offset %lld\n", *f_pos);
	    retval = 0; /* EOF */
	    goto unlock_and_return;
    }

    /* Determine How many bytes to copy */
    bytes_to_copy = min( count, entry->size - entry_offset_byte_rtn );

    /* Copy to user space */
    if ( copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, bytes_to_copy)) {
	    PDEBUG("Error copying data to user space\n");
	    retval = -EFAULT;
	    goto unlock_and_return;
    }

    /* Update file position */
    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

    PDEBUG("Read %zu bytes from entry (offset %zu, size %zu)", bytes_to_copy, entry_offset_byte_rtn, entry->size);

unlock_and_return:
    mutex_unlock(&dev->lock);
    return retval;

}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,loff_t *f_pos)
{
    ssize_t retval = -ENOMEM; /* Default return value for allocation failure */
    struct aesd_dev *dev = filp->private_data; /* Get the device structure */
    char *new_buffer = NULL; /* Temporary buffer for reallocations */
    const char *newline_pos = NULL; /* Pointer to newline character in input buffer */
    PDEBUG(" write %zu bytes with offset %lld\n",count, *f_pos);

    /* Lock the mutex to protect shared resources */
    if ( mutex_lock_interruptible(&dev->lock))
	    return -ERESTARTSYS;

    /* Append new data to the Partial Buffer */
    new_buffer = krealloc( dev->partial_write_buf, dev->partial_write_len + count, GFP_KERNEL);
    if( !new_buffer ) {
	    retval = -ENOMEM;
	    goto unlock_and_return;
    }
    dev->partial_write_buf = new_buffer;

    /* Copy data from user space into partial buffer */
    if ( copy_from_user (dev->partial_write_buf + dev->partial_write_len, buf, count)) {
	    retval = -EFAULT;
	    goto unlock_and_return;
    }
    dev->partial_write_len += count;

    /* Check the newline in the write */
    newline_pos = memchr( dev->partial_write_buf, '\n', dev->partial_write_len );
    if( newline_pos ) {
	    /* The write operation ends with newline (take advantage of the newline accumulation requirement ) */
	    size_t message_size = dev->partial_write_len;	/* Entire buffer size */

	    /* Allocate memory for the new circular buffer entry */
	    struct aesd_buffer_entry new_entry;
	    new_entry.buffptr = dev->partial_write_buf;
	    new_entry.size = message_size;

	    /* Add the entry to the circular buffer */
	    aesd_circular_buffer_add_entry(&dev->circ_buf, &new_entry);

	    /* Reset the partial write buffer */
	    dev->partial_write_buf = NULL;
	    dev->partial_write_len = 0;
    }

    retval = count; 	/* Return the number of bytes written */

unlock_and_return:
    mutex_unlock(&dev->lock);	/* Unlock the Mutex */
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
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circ_buf, index){
	    if( entry->buffptr ) {
		    PDEBUG("Freeing buffer entry at index %u", index);
		    kfree((char*)entry->buffptr);
		    entry->buffptr = NULL;
	    }
    }

    /*Free the partial write buffer if it exists */
    if( aesd_device.partial_write_buf ) {
	    PDEBUG("Freeing partial write buffer ");
	    kfree(aesd_device.partial_write_buf);
	    aesd_device.partial_write_buf = NULL;
    }

    /*Destroy the mutex */
    mutex_destroy (&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
