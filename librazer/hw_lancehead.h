#ifndef RAZER_HW_LANCEHEAD_H_
#define RAZER_HW_LANCEHEAD_H_

#include "razer_private.h"

int razer_lancehead_init(struct razer_mouse *m,
		     struct libusb_device *usbdev);
void razer_lancehead_release(struct razer_mouse *m);

#endif /* RAZER_HW_LANCEHEAD_H_ */
