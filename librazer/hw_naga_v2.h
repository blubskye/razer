#ifndef RAZER_HW_NAGA_V2_H_
#define RAZER_HW_NAGA_V2_H_

#include "razer_private.h"

int razer_naga_v2_init(struct razer_mouse *m,
		       struct libusb_device *usbdev);
void razer_naga_v2_release(struct razer_mouse *m);

#endif /* RAZER_HW_NAGA_V2_H_ */
