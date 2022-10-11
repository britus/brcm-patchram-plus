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
/*****************************************************************************
**
**  Name:          brcm_patchram_plus.c
**
**  Description:   This program downloads a patchram files in the HCD format
**                 to Broadcom Bluetooth based silicon and combo chips and
**				   and other utility functions.
**
**                 It can be invoked from the command line in the form
**						<-d> to print a debug log
**						<--patchram patchram_file>
**						<--baudrate baud_rate>
**						<--bd_addr bd_address>
**						<--enable_lpm>
**						<--enable_hci>
**						<--use_baudrate_for_download>
**						<--scopcm=sco_routing,pcm_interface_rate,frame_type,
**							sync_mode,clock_mode,lsb_first,fill_bits,
**							fill_method,fill_num,right_justify>
**
**							Where
**
**							sco_routing is 0 for PCM, 1 for Transport,
**							2 for Codec and 3 for I2S,
**
**							pcm_interface_rate is 0 for 128KBps, 1 for
**							256 KBps, 2 for 512KBps, 3 for 1024KBps,
**							and 4 for 2048Kbps,
**
**							frame_type is 0 for short and 1 for long,
**
**							sync_mode is 0 for slave and 1 for master,
**
**							clock_mode is 0 for slabe and 1 for master,
**
**							lsb_first is 0 for false aand 1 for true,
**
**							fill_bits is the value in decimal for unused bits,
**
**							fill_method is 0 for 0's and 1 for 1's, 2 for
**								signed and 3 for programmable,
**
**							fill_num is the number or bits to fill,
**
**							right_justify is 0 for false and 1 for true
**
**						<--i2s=i2s_enable,is_master,sample_rate,clock_rate>
**
**							Where
**
**							i2s_enable is 0 for disable and 1 for enable,
**
**							is_master is 0 for slave and 1 for master,
**
**							sample_rate is 0 for 8KHz, 1 for 16Khz and
**								2 for 4 KHz,
**
**							clock_rate is 0 for 128KHz, 1 for 256KHz, 3 for
**								1024 KHz and 4 for 2048 KHz.
**
**						<--no2bytes skips waiting for two byte confirmation
**							before starting patchram download. Newer chips
**                          do not generate these two bytes.>
**						<--tosleep=number of microsseconds to sleep before
**							patchram download begins.>
**						uart_device_name
**
**                 For example:
**
**                 brcm_patchram_plus -d --patchram  \
**						BCM2045B2_002.002.011.0348.0349.hcd /dev/ttyHS0
**
**                 It will return 0 for success and a number greater than 0
**                 for any errors.
**
**                 For Android, this program invoked using a
**                 "system(2)" call from the beginning of the bt_enable
**                 function inside the file
**                 system/bluetooth/bluedroid/bluetooth.c.
**
**                 If the Android system property "ro.bt.bcm_bdaddr_path" is
**                 set, then the bd_addr will be read from this path.
**                 This is overridden by --bd_addr on the command line.
**
******************************************************************************/
