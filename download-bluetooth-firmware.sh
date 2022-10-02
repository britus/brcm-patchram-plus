#!/bin/bash

TOOL="/bin/brcm_patchram_plus"
FWPATH="/lib/firmware/ap6212"

SERIALPORT="ttySAC1"

PID_BRCM=`ps --no-headers -C brcm_patchram_plus -o pid`
DEBUGFILE=`tempfile`_btdebug
> $DEBUGFILE

echo "FWFN=$FWFN" >> $DEBUGFILE

if [ -z $PID_BRCM ]; then

	for i in {1..30}; do
		if [ -d /sys/class/rfkill/rfkill0 ]; then
			break
		fi
		sleep 1
		echo "wait $is ..." >> $DEBUGFILE
	done

	/bin/chmod 0660 /sys/class/rfkill/rfkill0/state
	/bin/chmod 0660 /sys/class/rfkill/rfkill0/type
	/bin/chgrp dialout /sys/class/rfkill/rfkill0/state
	/bin/chgrp dialout /sys/class/rfkill/rfkill0/type
	/usr/sbin/rfkill block 0
	/usr/sbin/rfkill unblock 0

	MACADDRESS=`md5sum /sys/devices/platform/cpu/uuid | cut -b 1-12 | sed -r ':1;s/(.*[^:])([^:]{2})/\1:\2/;t1'`
	echo $MACADDRESS > /tmp/brcm_mac.txt

	if [ -e /dev/ttyAMA1 ]; then
		SERIALPORT=ttyAMA1
	fi

	echo "start brcm_patchram_plus ok." >> $DEBUGFILE

	$TOOL -d --patchram $FWPATH --enable_hci --bd_addr $MACADDRESS --no2bytes --tosleep 5000 /dev/${SERIALPORT}
fi

