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
#include "aesdchar.h"
#include <linux/rwlock.h>

#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("fed67"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

static DEFINE_RWLOCK(myrwlock);

struct aesd_dev aesd_device;

struct aesd_circular_buffer* my_ring_buf;

size_t read_complete = 0;
loff_t pos = 0;

//write
char* buffer = NULL;
size_t buffer_length = 0;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open 2");
    /**
     * TODO: handle open
     */
    read_complete = 0;
    // pos = 0;

    PDEBUG("Return file");

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld \n",count,*f_pos);

    PDEBUG("filep  %lld  mode %u flags %u ", filp->f_pos, filp->f_mode, filp->f_flags );

    size_t offset = *f_pos;
    loff_t read_size = min(get_total_size(my_ring_buf), count);
    /**
     * TODO: handle read
     */

    //file was read completly
    if(pos >= get_total_size(my_ring_buf)) {
        pos = 0;
        return 0;
    }

    char* buffer_loc = (char*) kmalloc(sizeof(char)*read_size, GFP_KERNEL);
        if(buffer_loc == NULL) {
        return -ENOMEM;
    }

    // PDEBUG("in_offs %u out_offs %u full %i \n", my_ring_buf->in_offs, my_ring_buf->out_offs, my_ring_buf->full );

    unsigned int flags = 0;
    read_lock_irqsave(&myrwlock, flags);

    size_t num_data_read = 0;
    PDEBUG("pos %u size %u \n", pos, read_size );
    for(size_t i = 0; i < read_size; ++i ) {
        size_t entry_offset_byte_rtn = 0;

        struct aesd_buffer_entry* ptr = aesd_circular_buffer_find_entry_offset_for_fpos(my_ring_buf, pos+offset+i, &entry_offset_byte_rtn);

        if(ptr == NULL) {
            PDEBUG("ptr == NULL \n" );
            read_unlock_irqrestore(&myrwlock, flags);
            pos += num_data_read;
            break;
            // return num_data_read; // returns EOF when using cat
        }
        PDEBUG("val >%c< \n", ptr->buffptr[entry_offset_byte_rtn] );
        buffer_loc[i] = ptr->buffptr[entry_offset_byte_rtn];
        num_data_read += 1;
    }
    pos += num_data_read;
    PDEBUG("after loop pos %u size %u \n", pos, read_size );

    read_unlock_irqrestore(&myrwlock, flags);

    if(copy_to_user(buf, buffer_loc, read_size))
        return -EFAULT; 

    read_complete = 1;

    return read_size;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

     unsigned int flags = 0;
     size_t size = count;

     char* buffer_loc = (char*) kmalloc(sizeof(char)*count, GFP_KERNEL);
     if(buffer_loc == NULL) {
        return -ENOMEM;
     }

     if(copy_from_user(buffer_loc, (int __user *)buf, count)) {
        kfree(buffer_loc);
        return -EFAULT;
     }

     //handle when not receiving a '\n' at the end
     if(buffer_loc[count-1] != '\n') {
        if(buffer != NULL) {
            void* result = (char*) krealloc(buffer, sizeof(char)*(count+buffer_length), GFP_KERNEL); //extend the buffer size
            if(result == NULL)
                return -ENOMEM;

            memcpy(buffer+buffer_length, buffer_loc, count);
            kfree(buffer_loc);
            buffer_length += count;
        } else {
            buffer = buffer_loc;
            buffer_length = count;
        }
        return count;
     }

     if(buffer != NULL) {
        // PDEBUG("Merging data\n");
        void* result = (char*) krealloc(buffer, sizeof(char)*(count+buffer_length), GFP_KERNEL); //extend the buffer size
        if(result == NULL)
            return -ENOMEM;

        memcpy(buffer+buffer_length, buffer_loc, count);
        kfree(buffer_loc);
        buffer_loc = buffer;
        buffer = NULL;
        buffer_length += count;

        size = buffer_length; // write all data
     }

     for(int i = 0; i < size; ++i) {
        // buffer_loc[i] = buf[i];
        PDEBUG("Write character: <%c> \n", buffer_loc[i]);
     }

     struct aesd_buffer_entry *add_entry = (struct aesd_buffer_entry *) kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
     if(add_entry == NULL) {
        kfree(buffer_loc);
        return -ENOMEM;
     }

     add_entry->buffptr = buffer_loc;
     add_entry->size = size;

     write_lock_irqsave(&myrwlock, flags);

     //free entry
     if(my_ring_buf->full) {
        kfree(my_ring_buf->entry[my_ring_buf->in_offs].buffptr);
     }

     aesd_circular_buffer_add_entry(my_ring_buf, add_entry);

     kfree(add_entry);

     write_unlock_irqrestore(&myrwlock, flags);

     PDEBUG("write return Count %d \n", count);
    
    return count;
}

//support llseek to determine the size of the buffer, needed for aesdsocket
static loff_t aesd_llseek(struct file *file, loff_t offset, int whence) {
    
    PDEBUG("lseek offset %i whence %i \n", offset, whence);

    switch (whence) {
        // SEEK_CUR and set are handeled equally
        case SEEK_SET: 
            pos = 0;
        case SEEK_CUR:  
            pos = offset;
            break;
        case SEEK_END:  
            pos = get_total_size(my_ring_buf)+offset;
            PDEBUG("pos %i \n", pos);
            break;
        default:
            return -EINVAL;
    }

    return pos;
}

static long test_ioctl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_seekto data;
    memset(&data, 0, sizeof(data));

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if(copy_from_user(&data, (int __user *)arg, sizeof(data))) {
                return -EFAULT;
            }
            PDEBUG("ioctl AESDCHAR_IOCSEEKTO \n");
            // pr_alert("IOCTL set val:%u offset %u .\n", data.write_cmd, data.write_cmd_offset);
            struct pair p;
            p.i = data.write_cmd;
            p.j = data.write_cmd_offset;
            PDEBUG("p i %d j %d \n", p.i, p.j);
            int result = get_positions(my_ring_buf, p);
            PDEBUG("ICTRL %d \n", result);

            if(result < 0) {
                return -EINVAL;
            }

            pos = result;
            break;
        default:
            return -ENOTTY;
    }
   
    PDEBUG("ICTRL return \n");
    return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = test_ioctl_ioctl,
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

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }


    my_ring_buf = (struct aesd_circular_buffer*) kmalloc(10*sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    my_ring_buf->in_offs = 0;
    my_ring_buf->out_offs = 0;
    my_ring_buf->full = false;

    if( my_ring_buf == NULL) {
        return -ENOMEM;
    }
    aesd_circular_buffer_init(my_ring_buf);

    buffer = NULL;
    buffer_length = 0;

    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    for(size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i) {
        kfree(my_ring_buf->entry[i].buffptr);
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
