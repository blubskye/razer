#ifndef RAZER_HW_VIPER_H_
#define RAZER_HW_VIPER_H_

#include "razer_private.h"

int razer_viper_init(struct razer_mouse *m,
		     struct libusb_device *usbdev);
void razer_viper_release(struct razer_mouse *m);

#endif /* RAZER_HW_VIPER_H_ */
