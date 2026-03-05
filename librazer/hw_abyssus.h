#ifndef RAZER_HW_ABYSSUS_H_
#define RAZER_HW_ABYSSUS_H_

#include "razer_private.h"

int razer_abyssus_init(struct razer_mouse *m,
		       struct libusb_device *usbdev);
void razer_abyssus_release(struct razer_mouse *m);

#endif /* RAZER_HW_ABYSSUS_H_ */
