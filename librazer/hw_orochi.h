#ifndef RAZER_HW_OROCHI_H_
#define RAZER_HW_OROCHI_H_

#include "razer_private.h"

int razer_orochi_init(struct razer_mouse *m,
		      struct libusb_device *usbdev);
void razer_orochi_release(struct razer_mouse *m);

#endif /* RAZER_HW_OROCHI_H_ */
