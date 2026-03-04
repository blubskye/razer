#ifndef RAZER_HW_BASILISK_H_
#define RAZER_HW_BASILISK_H_

#include "razer_private.h"

int razer_basilisk_init(struct razer_mouse *m,
			struct libusb_device *usbdev);
void razer_basilisk_release(struct razer_mouse *m);

#endif /* RAZER_HW_BASILISK_H_ */
