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
    filp->private_data = &aesd_device;
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
    ssize_t retval = 0;
    size_t total_size = 0;
    size_t read_offset = *f_pos;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    size_t bytes_available = 0;
    size_t bytes_to_copy = 0;
    size_t bytes_copied = 0;
    int err;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    mutex_lock(&dev->lock);
    //Calculate total size of all enteries in the circular buffer 
    {
    	int i;
    	int num_enteries = dev->circ_buf.full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : (dev->circ_buf.in_offs - dev->circ_buf.out_offs);
    	total_size = 0;
    	for( i = 0; i < num_enteries; i++ )
    	{
    	  	int pos = (dev->circ_buf.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    	  	total_size += dev->circ_buf.entry[pos].size;
    	}
    }
    
    if ( read_offset >= total_size ) {
    	mutex_unlock(&dev->lock);
    	return 0; //EOF
    }
    
    /* Use helper to locate the entry of current offset and copy out data */
    while ( count > 0 && (entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->circ_buf, read_offset, &entry_offset)) != NULL) {
    	bytes_available = entry->size - entry_offset;
    	bytes_to_copy = (count < bytes_available)?count : bytes_available;
    	err = copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy);
    	if (err != 0 ) {
    		mutex_unlock(&dev->lock);
    		return -EFAULT;
    	}
    	buf += bytes_to_copy;
    	count -= bytes_to_copy;
    	read_offset += bytes_to_copy;
    	bytes_copied += bytes_to_copy;
    }
    *f_pos = read_offset;
    mutex_unlock(&dev->lock);
    retval = bytes_copied;
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *kern_buf= NULL;
    char *new_cmd = NULL;
    size_t total_len;
    char *newline_ptr = NULL;
    size_t write_offset = 0;
    
    PDEBUG("write %zu bytes with offset %lld",count , *f_pos);
    
    kern_buf = kmalloc(count, GFP_KERNEL);
    if( !kern_buf) {
    	return -ENOMEM;
    }
    if( copy_from_user (kern_buf, buf, count) ) {
    	kfree(kern_buf);
    	return -EFAULT;
    }
    
    mutex_lock(&dev->lock);
    /*Append new data to any existing pending data */
    total_len = dev->partial_write_len + count;
    new_cmd = kmalloc(total_len, GFP_KERNEL);
    if( !new_cmd) {
    	kfree(kern_buf);
    	mutex_unlock(&dev->lock);
    	return -ENOMEM;
    }
    if ( dev->partial_write_buf ) {
    	memcpy(new_cmd, dev->partial_write_buf, dev->partial_write_len);
    	kfree(dev->partial_write_buf);
    }
    memcpy(new_cmd + dev->partial_write_len, kern_buf, count);
    kfree(kern_buf);
    dev->partial_write_buf = new_cmd;
    dev->partial_write_len = total_len;
    
    /* Process complete commands terminated by \n */
    while (( newline_ptr = memchr(dev->partial_write_buf + write_offset, '\n', dev->partial_write_len - write_offset )) != NULL ) {
    	size_t cmd_length = newline_ptr - (dev->partial_write_buf + write_offset ) + 1;
    	char *cmd_buf = kmalloc(cmd_length, GFP_KERNEL);
    	if( !cmd_buf ) {
    		mutex_unlock(&dev->lock);
    		return -ENOMEM;
    	}
    	memcpy(cmd_buf, dev->partial_write_buf + write_offset, cmd_length);
    	{
    		struct aesd_buffer_entry new_entry;
    		new_entry.buffptr = cmd_buf;
    		new_entry.size = cmd_length;
    		/*Add the entry and capture any replaced pointer */
    		const char *old_cmd = aesd_circular_buffer_add_entry(&dev->circ_buf, &new_entry);
    		if ( old_cmd )
    			kfree(old_cmd);
    	}
    	write_offset += cmd_length;
    }
    
    /*Handle Incomplete command */
    if (write_offset < dev->partial_write_len) {
    	size_t leftover = dev->partial_write_len - write_offset;
    	char *temp_buf = kmalloc(leftover, GFP_KERNEL);
    	if (!temp_buf) {
    		mutex_unlock(&dev->lock);
    		return -ENOMEM;
    	}
    	memcpy(temp_buf, dev->partial_write_buf + write_offset, leftover);
    	kfree(dev->partial_write_buf);
    	dev->partial_write_buf = temp_buf;
    	dev->partial_write_len = leftover;
    } else {
    	kfree ( dev->partial_write_buf );
    	dev->partial_write_buf = NULL;
    	dev->partial_write_len = 0;
    }
    mutex_unlock(&dev->lock);
    retval = count;
    return retval;
    		
}

/* New llseek implementation to support SEEK_SET, SEEK_CUR, and SEEK_END */
static loff_t aesd_llseek( struct file *filp, loff_t offset, int whence )
{
	struct aesd_dev *dev = filp->private_data;
	loff_t newpos;
	loff_t total_size = 0;
	int i, num_entries;
	
	PDEBUG("llseek: offset=%lld, whenc=%d", offset, whence);
	
	mutex_lock(&dev->lock);
	num_entries = dev->circ_buf.full?AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED:(dev->circ_buf.in_offs - dev->circ_buf.out_offs);
	for( i = 0; i < num_entries; i++ ) {
		int pos = (dev->circ_buf.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
		total_size += dev->circ_buf.entry[pos].size;
	}
	switch (whence) {
		case SEEK_SET:
			newpos = offset;
			break;
		case SEEK_CUR:
			newpos = filp->f_pos + offset;
			break;
		case SEEK_END:
			newpos = total_size + offset;
			break;
		default:
			mutex_unlock(&dev->lock);
			return -EINVAL;
			
	}
	if (newpos < 0 || newpos > total_size ) {
		mutex_unlock(&dev->lock);
		return -EINVAL;
	}
	filp->f_pos = newpos;
	mutex_unlock(&dev->lock);
	return newpos;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek = aesd_llseek,
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
