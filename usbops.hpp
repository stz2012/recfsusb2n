// USB操作

#ifndef _USB_OPS_HPP_
#define _USB_OPS_HPP_

#include <linux/version.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 4, 20)
#include <usb.h>
#include <linux/usb.h>
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 21)
#include <linux/usb_ch9.h>
#else
#include <linux/usb/ch9.h>
#endif
#include <linux/usbdevice_fs.h>
//#include <string>

void	usb_getdesc(const char *devfile, usb_device_descriptor* desc);
int	usb_open(const char *devfile);
void	usb_claim(int fd, unsigned int interface);
void	usb_release(int fd, unsigned int interface);
int	usb_setinterface(int fd, const unsigned int interface, const unsigned int altsetting);
int	usb_ctrl(int fd, usbdevfs_ctrltransfer *ctrl);
int	usb_submiturb(int fd, usbdevfs_urb* urbp);
int	usb_reapurb_ndelay(int fd, usbdevfs_urb** urbpp);
int	usb_discardurb(int fd, usbdevfs_urb* urbp);

#endif

