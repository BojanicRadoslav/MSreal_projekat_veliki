/*
LED radi lepo, trebalo bi da valja za sva 3 metoda unosa
Sintaksa je echo led (broj) > /dev/sqrt
Unos brojeva radi, mozda se unese 100 brojeva ako ne premase buffer od 500
Unos radi, pretvara brojeve u int i smesta u long int niz input_numbers
input_counter broji koliko ih ima
Sad treba stavljati u registre sqrt ip korova i odraditi interrupt kada zavrse da se stave novi
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

#include <linux/interrupt.h> //interrupt kada sqrt ip zavrsi sa racunanjem
#include <linux/spinlock.h> //ako vise ip korova zavrse u isto vreme i pokusavaju da pisu u niz resenja

#define BUFF_SIZE 500 //nek se nadje
#define DRIVER_NAME "sqrt"

#define X_offset 0x0
#define start_offset 0x4
#define Y_offset 0x8
#define ready_offset 0xc


MODULE_LICENSE("Dual BSD/GPL");

struct led_info {
  unsigned long mem_start;
  unsigned long mem_end;
  void __iomem *base_addr;
  int irq_num;
};

dev_t my_dev_id;
static spinlock_t lock = __SPIN_LOCK_UNLOCKED(lock);
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;
static struct led_info *lp = NULL;
static struct led_info *sqrtip0 = NULL;
static struct led_info *sqrtip1 = NULL;
static struct led_info *sqrtip2 = NULL;
static struct led_info *sqrtip3 = NULL;

static int led = 0;
static long int input_numbers[100];
static long int input_print[100];
static long int output_print[100];
static int input_count = 0; //broji koliko je uneto
static int output_count = 0; //broji koliko je izracunato
int endRead = 0;

static int sqrt0_ord;
static int sqrt1_ord;
static int sqrt2_ord;
static int sqrt3_ord;
static int finished = 0;

static irqreturn_t sqrt0_isr(int irq,void*dev_id); //interapt rutina kada sqrt0 zavrsi racunanje
static irqreturn_t sqrt1_isr(int irq,void*dev_id); //interapt rutina kada sqrt1 zavrsi racunanje
static irqreturn_t sqrt2_isr(int irq,void*dev_id); //interapt rutina kada sqrt2 zavrsi racunanje
static irqreturn_t sqrt3_isr(int irq,void*dev_id); //interapt rutina kada sqrt3 zavrsi racunanje
static int led_probe(struct platform_device *pdev);
static int led_remove(struct platform_device *pdev);
int led_open(struct inode *pinode, struct file *pfile);
int led_close(struct inode *pinode, struct file *pfile);
ssize_t led_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t led_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
static int __init led_init(void);
static void __exit led_exit(void);

static void sqrt0_write(int num)
{
      iowrite32((u32)num, sqrtip0->base_addr+X_offset);
      iowrite32((u32)1, sqrtip0-> base_addr + start_offset);
      iowrite32((u32)0, sqrtip0-> base_addr + start_offset);
}

static void sqrt1_write(int num)
{
      iowrite32((u32)num, sqrtip1->base_addr+X_offset);
      iowrite32((u32)1, sqrtip1-> base_addr + start_offset);
      iowrite32((u32)0, sqrtip1-> base_addr + start_offset);
}

static void sqrt2_write(int num)
{
      iowrite32((u32)num, sqrtip2->base_addr+X_offset);
      iowrite32((u32)1, sqrtip2-> base_addr + start_offset);
      iowrite32((u32)0, sqrtip2-> base_addr + start_offset);
}

static void sqrt3_write(int num)
{
      iowrite32((u32)num, sqrtip3->base_addr+X_offset);
      iowrite32((u32)1, sqrtip3-> base_addr + start_offset);
      iowrite32((u32)0, sqrtip3-> base_addr + start_offset);
}
struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_close,
};

static struct of_device_id led_of_match[] = {
  { .compatible = "led_gpio", },
  { .compatible = "xlnx,mysqrtip_0", },
  { .compatible = "xlnx,mysqrtip_1", },
  { .compatible = "xlnx,mysqrtip_2", },
  { .compatible = "xlnx,mysqrtip_3", },
  { /* end of list */ },
};

static struct platform_driver led_driver = {
  .driver = {
    .name = DRIVER_NAME,
    .owner = THIS_MODULE,
    .of_match_table	= led_of_match,
  },
  .probe		= led_probe,
  .remove		= led_remove,
};


MODULE_DEVICE_TABLE(of, led_of_match);

static irqreturn_t sqrt0_isr(int irq,void*dev_id)
{

  printk(KERN_INFO "\n\nSqrt0 interapt se desio\n\n");
  
  if(output_count < input_count){
    
    u32 led_val;
    led_val = ioread32(sqrtip0->base_addr+Y_offset);
    finished++;
    spin_lock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta
    output_print[sqrt0_ord] = (int)led_val;
    input_print[sqrt0_ord] = (int)input_numbers[sqrt0_ord];

    sqrt0_ord = output_count;
    sqrt0_write(input_numbers[output_count]);
    output_count++;
    spin_unlock(&lock);
}

  return IRQ_HANDLED;
}

static irqreturn_t sqrt1_isr(int irq,void*dev_id)
{
  printk(KERN_INFO "\n\nSqrt1 interapt se desio\n\n");
  
  if(output_count < input_count){
    
    u32 led_val;
    led_val = ioread32(sqrtip1->base_addr+Y_offset);
    finished++;
    spin_lock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta

    output_print[sqrt1_ord] = (int)led_val;
    input_print[sqrt1_ord] = (int)input_numbers[sqrt1_ord];

    sqrt1_ord = output_count;
    sqrt1_write(input_numbers[output_count]);
    output_count++;
    spin_unlock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta

}
  return IRQ_HANDLED;
}

static irqreturn_t sqrt2_isr(int irq,void*dev_id)
{
  printk(KERN_INFO "\n\nSqrt2 interapt se desio\n\n");
  
  if(output_count < input_count){
    
    u32 led_val;
    led_val = ioread32(sqrtip2->base_addr+Y_offset);
    finished++;
    spin_lock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta

    output_print[sqrt2_ord] = (int)led_val;
    input_print[sqrt2_ord] = (int)input_numbers[sqrt2_ord];

    sqrt2_ord = output_count;
    sqrt2_write(input_numbers[output_count]);
    output_count++;
    spin_unlock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta

}
  return IRQ_HANDLED;
}

static irqreturn_t sqrt3_isr(int irq,void*dev_id)
{
  printk(KERN_INFO "\n\nSqrt3 interapt se desio\n\n");
  
  if(output_count < input_count){
    
    u32 led_val;
    led_val = ioread32(sqrtip3->base_addr+Y_offset);
    finished++;
    spin_lock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta

    output_print[sqrt3_ord] = (int)led_val;
    input_print[sqrt3_ord] = (int)input_numbers[sqrt3_ord];

    sqrt3_ord = output_count;
    sqrt3_write(input_numbers[output_count]);
    output_count++;
    spin_unlock(&lock); //output_print,input_print,output_count su deljeni resursi pa ih treba zakljucati da ne dodje do konflikta


}
  return IRQ_HANDLED;
}

static int led_probe(struct platform_device *pdev)
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
   lp = (struct led_info *) kmalloc(sizeof(struct led_info), GFP_KERNEL);
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
   sqrtip0 = (struct led_info *) kmalloc(sizeof(struct led_info), GFP_KERNEL);
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

  if (request_irq(sqrtip0->irq_num, sqrt0_isr, 0, DRIVER_NAME, NULL)) {
    printk(KERN_ERR "xilaxitimer_probe: Cannot register IRQ %d\n", sqrtip0->irq_num);
    rc = -EIO;
    goto irq1;
  
  }
  else {
    //enable_irq(sqrtip0->irq_num);
    printk(KERN_INFO "xilaxitimer_probe: Registered IRQ %d\n", sqrtip0->irq_num);
  }
 }else if(counter == 3){
   sqrtip1 = (struct led_info *) kmalloc(sizeof(struct led_info), GFP_KERNEL);
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

  if (request_irq(sqrtip1->irq_num, sqrt1_isr, 0, DRIVER_NAME, NULL)) {
    printk(KERN_ERR "xilaxitimer_probe: Cannot register IRQ %d\n", sqrtip1->irq_num);
    rc = -EIO;
    goto irq2;
  
  }
  else {
    //enable_irq(sqrtip0->irq_num);
    printk(KERN_INFO "xilaxitimer_probe: Registered IRQ %d\n", sqrtip1->irq_num);
  }
 }else if(counter == 4){
   sqrtip2 = (struct led_info *) kmalloc(sizeof(struct led_info), GFP_KERNEL);
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

  if (request_irq(sqrtip2->irq_num, sqrt2_isr, 0, DRIVER_NAME, NULL)) {
    printk(KERN_ERR "xilaxitimer_probe: Cannot register IRQ %d\n", sqrtip2->irq_num);
    rc = -EIO;
    goto irq3;
  
  }
  else {
    //enable_irq(sqrtip0->irq_num);
    printk(KERN_INFO "xilaxitimer_probe: Registered IRQ %d\n", sqrtip2->irq_num);
  }
 }else if(counter == 5){
   sqrtip3 = (struct led_info *) kmalloc(sizeof(struct led_info), GFP_KERNEL);
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

  if (request_irq(sqrtip3->irq_num, sqrt3_isr, 0, DRIVER_NAME, NULL)) {
    printk(KERN_ERR "xilaxitimer_probe: Cannot register IRQ %d\n", sqrtip3->irq_num);
    rc = -EIO;
    goto irq4;
  
  }
  else {
    //enable_irq(sqrtip0->irq_num);
    printk(KERN_INFO "xilaxitimer_probe: Registered IRQ %d\n", sqrtip3->irq_num);
  }
 }

  printk(KERN_WARNING "led platform driver registered\n");
  return 0;//ALL OK
irq4:
    iounmap(sqrtip3->base_addr);
error6:
    release_mem_region(sqrtip3->mem_start, sqrtip3->mem_end - sqrtip3->mem_start + 1);
irq3:
    iounmap(sqrtip2->base_addr);
error5:
    release_mem_region(sqrtip2->mem_start, sqrtip2->mem_end - sqrtip2->mem_start + 1);
irq2:
    iounmap(sqrtip1->base_addr);
error4:
    release_mem_region(sqrtip1->mem_start, sqrtip1->mem_end - sqrtip1->mem_start + 1);
irq1:
    iounmap(sqrtip0->base_addr);
error3:
    release_mem_region(sqrtip0->mem_start, sqrtip0->mem_end - sqrtip0->mem_start + 1);
error2:
  release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
error1:
  return rc;
}

static int led_remove(struct platform_device *pdev)
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

    free_irq(sqrtip0->irq_num, NULL);

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
  free_irq(sqrtip1->irq_num, NULL);

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
  free_irq(sqrtip2->irq_num, NULL);

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
  free_irq(sqrtip3->irq_num, NULL);

  release_mem_region(sqrtip3->mem_start, sqrtip3->mem_end - sqrtip3->mem_start + 1);
  kfree(sqrtip3);
  printk(KERN_WARNING "sqrt IP3 is free\n");
}

  return 0;
}



int led_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened sqrt\n");
		return 0;
}

int led_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed sqrt\n");
		return 0;
}

ssize_t led_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	int len = 0;
	u32 led_val = 0;
	int i = 0;
	char buff[BUFF_SIZE];
	if (endRead){
		endRead = 0;
		return 0;
	}


	//buffer: 0b????
	//index:  012345
  sprintf(buff, "finished: %d\n", finished);
	len=15;
	ret = copy_to_user(buffer, buff, len);
	if(ret)
		return -EFAULT;
	//printk(KERN_INFO "Succesfully read\n");
	endRead = 1;

	return len;
}

ssize_t led_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
	char buff[BUFF_SIZE];
	int ret = 0;
	long int led_val=0;
  int i = 0; //treba mi za for ciklus dole

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
  		ret = kstrtol(buff+6,16,&led_val);
      printk(KERN_INFO "Uneto bin\n");
  	}
  	// BINARY INPUT
  	else if(buff[4] == '0'  && (buff[5] == 'b' || buff[5] == 'B')) 
  	{
  		ret = kstrtol(buff+6,2,&led_val);
  	}
  	// DECIMAL INPUT
  	else 
  	{
  		ret = kstrtol(buff+4,10,&led_val);
  	}

    printk(KERN_INFO "Uneto %ld\n", led_val);

}else{

    char* const delim = ",";
    //char str[] = "some/split/string";
    char *token, *cur = buff;
    while (token = strsep(&cur, delim)) {
      printk(KERN_INFO "%s\n", token);
      ret = kstrtol(token, 10, &input_numbers[input_count]);
      input_count++;
    }
    
    for(i=0;i<input_count;i++) printk(KERN_INFO "%d. = %ld\n", i, input_numbers[i]);
      if(input_count >= 4){
        sqrt0_ord = output_count;
        sqrt0_write(input_numbers[output_count]);
        output_count++;

        sqrt1_ord = output_count;
        sqrt1_write(input_numbers[output_count]);
        output_count++;

        sqrt2_ord = output_count;
        sqrt2_write(input_numbers[output_count]);
        output_count++;

        sqrt3_ord = output_count;
        sqrt3_write(input_numbers[output_count]);
        output_count++;
      }else if(input_count == 3){
        sqrt0_ord = output_count;
        sqrt0_write(input_numbers[output_count]);
        output_count++;

        sqrt1_ord = output_count;
        sqrt1_write(input_numbers[output_count]);
        output_count++;

        sqrt2_ord = output_count;
        sqrt2_write(input_numbers[output_count]);
        output_count++;
      }else if(input_count == 2){
        sqrt0_ord = output_count;
        sqrt0_write(input_numbers[output_count]);
        output_count++;

        sqrt1_ord = output_count;
        sqrt1_write(input_numbers[output_count]);
        output_count++;
      }else if(input_count == 1){
        sqrt0_ord = output_count;
        sqrt0_write(input_numbers[output_count]);
        output_count++;
      }
}

    if (!ret)
    {
      if(led == 1)
      {
        iowrite32((u32)led_val, lp->base_addr);
      }
      //printk(KERN_INFO "Succesfully wrote value %#x",(u32)led_val); 
    }
    else
    {
      printk(KERN_INFO "Wrong command format\n"); 
    }

	return length;
}

static int __init led_init(void)
{
   int ret = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, DRIVER_NAME);
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "led_class");
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

  return platform_driver_register(&led_driver);

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit led_exit(void)
{
   platform_driver_unregister(&led_driver);
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(led_init);
module_exit(led_exit);
