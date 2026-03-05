#ifndef RAZER_HW_MAMBA_WIRELESS_H_
#define RAZER_HW_MAMBA_WIRELESS_H_

#include "razer_private.h"

int razer_mamba_wireless_init(struct razer_mouse *m,
		     struct libusb_device *usbdev);
void razer_mamba_wireless_release(struct razer_mouse *m);

#endif /* RAZER_HW_MAMBA_WIRELESS_H_ */
