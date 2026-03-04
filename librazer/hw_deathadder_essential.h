#ifndef RAZER_HW_DEATHADDER_ESSENTIAL_H_
#define RAZER_HW_DEATHADDER_ESSENTIAL_H_

#include "razer_private.h"

int razer_deathadder_essential_init(struct razer_mouse *m,
				    struct libusb_device *usbdev);

void razer_deathadder_essential_release(struct razer_mouse *m);

#endif /* RAZER_HW_DEATHADDER_ESSENTIAL_H_ */
