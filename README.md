brcm-patchram-plus
==================

Patching BCM4343 Bluetooth chip. FriendlyElec Smart6818 using AP6212 
wireless & bluetooth chip. The BCM4343 is part of the AP6212.

## Overview
- HCI uart i/o interface
- Write firmware file to chips.
- Update MAC address
- Update baudrate
- Configure protocol HCI_UART_H4


## System
Command line see also in file download-bluetooth-firmware.sh

```

PORT="ttySAC1"
FWPATH="/lib/firmware/ap6212"
TOOL="/bin/brcm_patchram_plus"
MAC=`md5sum /sys/devices/platform/cpu/uuid | cut -b 1-12 | sed -r ':1;s/(.*[^:])([^:]{2})/\1:\2/;t1'`

$TOOL -d --patchram $FWPATH --enable_hci --bd_addr $MAC --no2bytes --tosleep 5000 /dev/${PORT}

```
