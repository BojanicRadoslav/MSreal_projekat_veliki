/*
Format unosenja brojeva je echo 1,2,3,4 > /dev/sqrt &, mora & obavezno inace zabode terminal
Ako ima vise od 4 broja, 4 se stave u IP jezgra a ostali cekaju da se ovi iscitaju
Kada se obradjeni iscitaju u IP jezgra sledeca 4 broja ili 3,2,1 zavisi koliko ih je ostalo
Ledovke na plocici umesto da pokazuju koje jezgro je zauzeto pokazuju koliko brojeva jos treba da se iscita, nije moglo drugacije
Primer kako treba da se koristi drajver:
echo 1,2,3,4,5 > /dev/sqrt &, pale se 4 led diode
cat /dev/sqrt
1:1,2:1,3:1,4:2, pali se 1 led dioda
cat /dev/sqrt
5:2, sve led diode ugasene
*/


#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/of.h>

#include <linux/io.h> //iowrite ioread
#include <linux/slab.h>//kmalloc kfree
#include <linux/platform_device.h>//platform driver
#include <linux/of.h>//of match table
#include <linux/ioport.h>//ioremap

#include <linux/wait.h>

#define BUFF_SIZE 500 //nek se nadje
#define DRIVER_NAME "sqrt"

#define X_offset 0x0
#define start_offset 0x4
#define Y_offset 0x8
#define ready_offset 0xc


MODULE_LICENSE("Dual BSD/GPL");

struct sqrt_info {
  unsigned long mem_start;
  unsigned long mem_end;
  void __iomem *base_addr;
  int irq_num;
};

wait_queue_head_t queue;
static int queue_flag = 0;

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;
static struct sqrt_info *lp = NULL;
static struct sqrt_info *sqrtip0 = NULL;
static struct sqrt_info *sqrtip1 = NULL;
static struct sqrt_info *sqrtip2 = NULL;
static struct sqrt_info *sqrtip3 = NULL;

static int led = 0;
static long int input_numbers[100];
static int input_count = 0; //broji koliko je uneto
static int output_count = 0; //broji koliko je izracunato
int endRead = 0;

static int finished = 0;

static int sqrt_probe(struct platform_device *pdev);
static int sqrt_remove(struct platform_device *pdev);
int sqrt_open(struct inode *pinode, struct file *pfile);
int sqrt_close(struct inode *pinode, struct file *pfile);
ssize_t sqrt_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t sqrt_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
static int __init sqrt_init(void);
static void __exit sqrt_exit(void);

static void led_on(int sqrt)
{
  u32 led;
  switch(sqrt)
  {
    case 1:
      led = 1;
      break;
    case 2:
      led = 3;
      break;
    case 3:
      led = 7;
      break;
    case 4:
      led = 15;
      break;
    default:
      led = 0;
      break;

  }
  iowrite32((u32)led, lp->base_addr);
}

static void sqrt_write_num(int num, int sqrt)
{
      if(sqrt == 0)
      {
        iowrite32((u32)num, sqrtip0->base_addr+X_offset);
        iowrite32((u32)1, sqrtip0-> base_addr + start_offset);
        iowrite32((u32)0, sqrtip0-> base_addr + start_offset);
      }else if(sqrt == 1)
      {
        iowrite32((u32)num, sqrtip1->base_addr+X_offset);
        iowrite32((u32)1, sqrtip1-> base_addr + start_offset);
        iowrite32((u32)0, sqrtip1-> base_addr + start_offset);

      }else if(sqrt == 2)
      {
        iowrite32((u32)num, sqrtip2->base_addr+X_offset);
        iowrite32((u32)1, sqrtip2-> base_addr + start_offset);
        iowrite32((u32)0, sqrtip2-> base_addr + start_offset);

      }else if(sqrt == 3)
      {
        iowrite32((u32)num, sqrtip3->base_addr+X_offset);
        iowrite32((u32)1, sqrtip3-> base_addr + start_offset);
        iowrite32((u32)0, sqrtip3-> base_addr + start_offset);
      }

      printk(KERN_INFO "Written to sqrt%d\n", sqrt);
      
}
struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = sqrt_open,
	.read = sqrt_read,
	.write = sqrt_write,
	.release = sqrt_close,
};

static struct of_device_id sqrt_of_match[] = {
  { .compatible = "led_gpio", },
  { .compatible = "xlnx,mysqrtip_0", },
  { .compatible = "xlnx,mysqrtip_1", },
  { .compatible = "xlnx,mysqrtip_2", },
  { .compatible = "xlnx,mysqrtip_3", },
  { /* end of list */ },
};

static struct platform_driver sqrt_driver = {
  .driver = {
    .name = DRIVER_NAME,
    .owner = THIS_MODULE,
    .of_match_table	= sqrt_of_match,
  },
  .probe		= sqrt_probe,
  .remove		= sqrt_remove,
};


MODULE_DEVICE_TABLE(of, sqrt_of_match);


static int sqrt_probe(struct platform_device *pdev)
{
  struct resource *r_mem;
  int rc = 0;
  static int counter = 0;
  counter++;
  r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (!r_mem) {
    printk(KERN_ALERT "Failed to get resource\n");
    return -ENODEV;
  }
  if(counter == 1){
   lp = (struct sqrt_info *) kmalloc(sizeof(struct sqrt_info), GFP_KERNEL);
   if (!lp) {
     printk(KERN_ALERT "Could not allocate led device\n");
     return -ENOMEM;
   }

   lp->mem_start = r_mem->start;
   lp->mem_end = r_mem->end;
  //printk(KERN_INFO "Start address:%x \t end address:%x", r_mem->start, r_mem->end);

   if (!request_mem_region(lp->mem_start,lp->mem_end - lp->mem_start + 1,	DRIVER_NAME))
   {
     printk(KERN_ALERT "Could not lock memory region at %p\n",(void *)lp->mem_start);
     rc = -EBUSY;
     goto error1;
   }

   lp->base_addr = ioremap(lp->mem_start, lp->mem_end - lp->mem_start + 1);
   if (!lp->base_addr) {
     printk(KERN_ALERT "Could not allocate memory\n");
     rc = -EIO;
     goto error2;
   }
 }else if(counter == 2){
   sqrtip0 = (struct sqrt_info *) kmalloc(sizeof(struct sqrt_info), GFP_KERNEL);
   if (!sqrtip0) {
     printk(KERN_ALERT "Could not allocate led device\n");
     return -ENOMEM;
   }

   sqrtip0->mem_start = r_mem->start;
   sqrtip0->mem_end = r_mem->end;
  //printk(KERN_INFO "Start address:%x \t end address:%x", r_mem->start, r_mem->end);

   if (!request_mem_region(sqrtip0->mem_start,sqrtip0->mem_end - sqrtip0->mem_start + 1,  DRIVER_NAME))
   {
     printk(KERN_ALERT "Could not lock memory region at %p\n",(void *)sqrtip0->mem_start);
     rc = -EBUSY;
     goto error1;
   }

   sqrtip0->base_addr = ioremap(sqrtip0->mem_start, sqrtip0->mem_end - sqrtip0->mem_start + 1);
   if (!sqrtip0->base_addr) {
     printk(KERN_ALERT "Could not allocate memory\n");
     rc = -EIO;
     goto error3;
   }

   // Get interrupt number from device tree
  sqrtip0->irq_num = platform_get_irq(pdev, 0);
  if (!sqrtip0->irq_num) {
    printk(KERN_ALERT "xilaxitimer_probe: Failed to get irq resource\n");
    rc = -ENODEV;
    goto error3;
  }

 }else if(counter == 3){
   sqrtip1 = (struct sqrt_info *) kmalloc(sizeof(struct sqrt_info), GFP_KERNEL);
   if (!sqrtip1) {
     printk(KERN_ALERT "Could not allocate led device\n");
     return -ENOMEM;
   }

   sqrtip1->mem_start = r_mem->start;
   sqrtip1->mem_end = r_mem->end;
  //printk(KERN_INFO "Start address:%x \t end address:%x", r_mem->start, r_mem->end);

   if (!request_mem_region(sqrtip1->mem_start,sqrtip1->mem_end - sqrtip1->mem_start + 1,  DRIVER_NAME))
   {
     printk(KERN_ALERT "Could not lock memory region at %p\n",(void *)sqrtip1->mem_start);
     rc = -EBUSY;
     goto error1;
   }

   sqrtip1->base_addr = ioremap(sqrtip1->mem_start, sqrtip1->mem_end - sqrtip1->mem_start + 1);
   if (!sqrtip1->base_addr) {
     printk(KERN_ALERT "Could not allocate memory\n");
     rc = -EIO;
     goto error4;
   }

  sqrtip1->irq_num = platform_get_irq(pdev, 0);
  if (!sqrtip1->irq_num) {
    printk(KERN_ALERT "xilaxitimer_probe: Failed to get irq resource\n");
    rc = -ENODEV;
    goto error4;
  }

 }else if(counter == 4){
   sqrtip2 = (struct sqrt_info *) kmalloc(sizeof(struct sqrt_info), GFP_KERNEL);
   if (!sqrtip2) {
     printk(KERN_ALERT "Could not allocate led device\n");
     return -ENOMEM;
   }

   sqrtip2->mem_start = r_mem->start;
   sqrtip2->mem_end = r_mem->end;
  //printk(KERN_INFO "Start address:%x \t end address:%x", r_mem->start, r_mem->end);

   if (!request_mem_region(sqrtip2->mem_start,sqrtip2->mem_end - sqrtip2->mem_start + 1,  DRIVER_NAME))
   {
     printk(KERN_ALERT "Could not lock memory region at %p\n",(void *)sqrtip2->mem_start);
     rc = -EBUSY;
     goto error1;
   }

   sqrtip2->base_addr = ioremap(sqrtip2->mem_start, sqrtip2->mem_end - sqrtip2->mem_start + 1);
   if (!sqrtip2->base_addr) {
     printk(KERN_ALERT "Could not allocate memory\n");
     rc = -EIO;
     goto error5;
   }

   sqrtip2->irq_num = platform_get_irq(pdev, 0);
  if (!sqrtip2->irq_num) {
    printk(KERN_ALERT "xilaxitimer_probe: Failed to get irq resource\n");
    rc = -ENODEV;
    goto error5;
  }

 }else if(counter == 5){
   sqrtip3 = (struct sqrt_info *) kmalloc(sizeof(struct sqrt_info), GFP_KERNEL);
   if (!sqrtip3) {
     printk(KERN_ALERT "Could not allocate led device\n");
     return -ENOMEM;
   }

   sqrtip3->mem_start = r_mem->start;
   sqrtip3->mem_end = r_mem->end;
  //printk(KERN_INFO "Start address:%x \t end address:%x", r_mem->start, r_mem->end);

   if (!request_mem_region(sqrtip3->mem_start,sqrtip3->mem_end - sqrtip3->mem_start + 1,  DRIVER_NAME))
   {
     printk(KERN_ALERT "Could not lock memory region at %p\n",(void *)sqrtip3->mem_start);
     rc = -EBUSY;
     goto error1;
   }

   sqrtip3->base_addr = ioremap(sqrtip3->mem_start, sqrtip3->mem_end - sqrtip3->mem_start + 1);
   if (!sqrtip3->base_addr) {
     printk(KERN_ALERT "Could not allocate memory\n");
     rc = -EIO;
     goto error6;
   }

   sqrtip3->irq_num = platform_get_irq(pdev, 0);
  if (!sqrtip3->irq_num) {
    printk(KERN_ALERT "xilaxitimer_probe: Failed to get irq resource\n");
    rc = -ENODEV;
    goto error6;
  }

 }

  printk(KERN_WARNING "led platform driver registered\n");
  return 0;//ALL OK
error6:
    release_mem_region(sqrtip3->mem_start, sqrtip3->mem_end - sqrtip3->mem_start + 1);
error5:
    release_mem_region(sqrtip2->mem_start, sqrtip2->mem_end - sqrtip2->mem_start + 1);
error4:
    release_mem_region(sqrtip1->mem_start, sqrtip1->mem_end - sqrtip1->mem_start + 1);
error3:
    release_mem_region(sqrtip0->mem_start, sqrtip0->mem_end - sqrtip0->mem_start + 1);
error2:
  release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
error1:
  return rc;
}

static int sqrt_remove(struct platform_device *pdev)
{

  static int counter_remove = 0;
  counter_remove++;
  //printk(KERN_WARNING "Counter remove %d\n", counter_remove);

  //printk(KERN_WARNING "led platform driver removed\n");
  if(counter_remove == 1){
    iowrite32(0, lp->base_addr);
    iounmap(lp->base_addr);
    release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
    kfree(lp);
    printk(KERN_WARNING "lp is free\n");
  }else if(counter_remove == 2){

    iowrite32(0, sqrtip0->base_addr+X_offset);
    iounmap(sqrtip0->base_addr+X_offset);
    iowrite32(0, sqrtip0->base_addr+start_offset);
    iounmap(sqrtip0->base_addr+start_offset);
    iowrite32(0, sqrtip0->base_addr+Y_offset);
    iounmap(sqrtip0->base_addr+Y_offset);
    iowrite32(0, sqrtip0->base_addr+ready_offset);
    iounmap(sqrtip0->base_addr+ready_offset);


    release_mem_region(sqrtip0->mem_start, sqrtip0->mem_end - sqrtip0->mem_start + 1);
    kfree(sqrtip0);
    printk(KERN_WARNING "sqrt IP0 is free\n");

}else if(counter_remove == 3){
  
 
  iowrite32(0, sqrtip1->base_addr+X_offset);
  iowrite32(0, sqrtip1->base_addr+start_offset);
  iowrite32(0, sqrtip1->base_addr+Y_offset);
  iowrite32(0, sqrtip1->base_addr+ready_offset);
  iounmap(sqrtip1->base_addr+X_offset);
  iounmap(sqrtip1->base_addr+start_offset);
  iounmap(sqrtip1->base_addr+Y_offset);
  iounmap(sqrtip1->base_addr+ready_offset);

  release_mem_region(sqrtip1->mem_start, sqrtip1->mem_end - sqrtip1->mem_start + 1);
  kfree(sqrtip1);
  printk(KERN_WARNING "sqrt IP1 is free\n");
}else if(counter_remove == 4){

  iowrite32(0, sqrtip2->base_addr+X_offset);
  iowrite32(0, sqrtip2->base_addr+start_offset);
  iowrite32(0, sqrtip2->base_addr+Y_offset);
  iowrite32(0, sqrtip2->base_addr+ready_offset);
  iounmap(sqrtip2->base_addr+X_offset);
  iounmap(sqrtip2->base_addr+start_offset);
  iounmap(sqrtip2->base_addr+Y_offset);
  iounmap(sqrtip2->base_addr+ready_offset);

  release_mem_region(sqrtip2->mem_start, sqrtip2->mem_end - sqrtip2->mem_start + 1);
  kfree(sqrtip2);
  printk(KERN_WARNING "sqrt IP2 is free\n");
}else if(counter_remove == 5){

  iowrite32(0, sqrtip3->base_addr+X_offset);
  iowrite32(0, sqrtip3->base_addr+start_offset);
  iowrite32(0, sqrtip3->base_addr+Y_offset);
  iowrite32(0, sqrtip3->base_addr+ready_offset);
  iounmap(sqrtip3->base_addr+X_offset);
  iounmap(sqrtip3->base_addr+start_offset);
  iounmap(sqrtip3->base_addr+Y_offset);
  iounmap(sqrtip3->base_addr+ready_offset);

  release_mem_region(sqrtip3->mem_start, sqrtip3->mem_end - sqrtip3->mem_start + 1);
  kfree(sqrtip3);
  printk(KERN_WARNING "sqrt IP3 is free\n");
}

  return 0;
}



int sqrt_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened sqrt\n");
		return 0;
}

int sqrt_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed sqrt\n");
		return 0;
}

ssize_t sqrt_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	int len = 0;
	u32 in = 0;
  u32 out = 0;
	char buff[BUFF_SIZE];
  char output[BUFF_SIZE];
	if (endRead){
		endRead = 0;
		return 0;
	}

  sprintf(buff, "");

  if(finished < 4) led_on(0);

  if(finished >= 1){

    in = ioread32(sqrtip0->base_addr+X_offset);
    out = ioread32(sqrtip0->base_addr+Y_offset);
    sprintf(output, "%d:%d,", in,out);
    strcat(buff, output);

    if(finished >= 2)
    {
      in = ioread32(sqrtip1->base_addr+X_offset);
      out = ioread32(sqrtip1->base_addr+Y_offset);
      sprintf(output, "%d:%d,", in,out);
      strcat(buff, output);
      if(finished >= 3)
      {
        in = ioread32(sqrtip2->base_addr+X_offset);
        out = ioread32(sqrtip2->base_addr+Y_offset);
        sprintf(output, "%d:%d,", in,out);
        strcat(buff, output);

        if(finished >= 4)
        {
          in = ioread32(sqrtip3->base_addr+X_offset);
          out = ioread32(sqrtip3->base_addr+Y_offset);
          sprintf(output, "%d:%d,", in,out);
          strcat(buff, output);
        }
      }
    }
  }
  finished = 0;
  queue_flag = 0;
  wake_up_interruptible(&queue);


	//buffer: 0b????
	//index:  012345
  //sprintf(buff, "finished: %d\n", finished);
  buff[strlen(buff)-1] = '\0';
  strcat(buff, "\n");
	len=strlen(buff);
	ret = copy_to_user(buffer, buff, len);
	if(ret)
		return -EFAULT;
	//printk(KERN_INFO "Succesfully read\n");
	endRead = 1;

	return len;
}

ssize_t sqrt_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
	char buff[BUFF_SIZE];
	int ret = 0;
	long int sqrt_val=0;

	ret = copy_from_user(buff, buffer, length);
	if(ret)
		return -EFAULT;
	buff[length] = '\0';
  //poziva se kao echo led 0b111 > /dev/sqrt
  if(buff[0] == 'l' || buff[0] == 'L'){
    if(buff[1] == 'e' || buff[1] == 'E'){
      if(buff[2] == 'd' || buff[0] == 'D'){
        led = 1;
        //printk(KERN_INFO "Uneto LED\n");
      }
    }
  }else led = 0;

  if(led == 1){

  	// HEX  INPUT
  	if(buff[4] == '0' && (buff[5] == 'x' || buff[5] == 'X')) 
  	{
  		ret = kstrtol(buff+6,16,&sqrt_val);
      printk(KERN_INFO "Uneto bin\n");
  	}
  	// BINARY INPUT
  	else if(buff[4] == '0'  && (buff[5] == 'b' || buff[5] == 'B')) 
  	{
  		ret = kstrtol(buff+6,2,&sqrt_val);
  	}
  	// DECIMAL INPUT
  	else 
  	{
  		ret = kstrtol(buff+4,10,&sqrt_val);
  	}

    printk(KERN_INFO "Uneto %ld\n", sqrt_val);

}else{
    input_count = 0;
    output_count = 0;
    finished = 0;
    char* const delim = ",";
    int n;
    //char str[] = "some/split/string";
    char *token, *cur = buff;
    int i;
    while (token = strsep(&cur, delim)) {
      printk(KERN_INFO "%s\n", token);
      ret = kstrtol(token, 10, &input_numbers[input_count]);
      input_count++;
    }
    
    for(i=0;i<input_count;i++) printk(KERN_INFO "%d. = %ld\n", i, input_numbers[i]);
      
      finished = 0;
      queue_flag = 0;
      while(output_count < input_count)
      {

        if(wait_event_interruptible(queue, (queue_flag == 0)))
        {
          return -ERESTARTSYS;
        }
        queue_flag = 1;
        if(input_count-output_count > 4) n = 4;
        else n = input_count-output_count;
        for(i=0;i<n;i++)
        {
          sqrt_write_num(input_numbers[output_count], output_count%4);
          output_count++;
          finished++;
        }
        led_on(finished);
      }
}

    if (!ret)
    {
      if(led == 1)
      {
        iowrite32((u32)sqrt_val, lp->base_addr);
      }
      //printk(KERN_INFO "Succesfully wrote value %#x",(u32)sqrt_val); 
    }
    else
    {
      printk(KERN_INFO "Wrong command format\n"); 
    }

	return length;
}

static int __init sqrt_init(void)
{
   int ret = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, DRIVER_NAME);
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "sqrt_class");
   if (my_class == NULL){
      printk(KERN_ERR "failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");
   
   my_device = device_create(my_class, NULL, my_dev_id, NULL, DRIVER_NAME);
   if (my_device == NULL){
      printk(KERN_ERR "failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
      printk(KERN_ERR "failed to add cdev\n");
		goto fail_2;
	}
   printk(KERN_INFO "cdev added\n");
   printk(KERN_INFO "Hello world\n");

   init_waitqueue_head(&queue);

  return platform_driver_register(&sqrt_driver);

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit sqrt_exit(void)
{
   platform_driver_unregister(&sqrt_driver);
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(sqrt_init);
module_exit(sqrt_exit);
