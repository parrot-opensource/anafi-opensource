#!/bin/sh

TRIGGER_NAME=dump_iio_sensors
TRIGGER_DIR=/dev/cfg/iio/triggers/hrtimer/${TRIGGER_NAME}

function setup_channel () {
	local sensor=$1
	local channel=$2
	local meta_file=$(echo -n "${sensor}" | sed 's/[<>:"\\|?*]/_/g').ini
	local sysfs_path=/sys/bus/iio/devices/$sensor

	echo "Setup channel $channel"
	echo "[${channel}]" >> $meta_file
	echo -n "index = " >> $meta_file
	cat $sysfs_path/scan_elements/${channel}_index >> $meta_file
	echo -n "type = " >> $meta_file
	cat $sysfs_path/scan_elements/${channel}_type >> $meta_file
	if [[ -e $sysfs_path/${channel}_scale ]]; then
		echo -n "scale = " >> $meta_file
		cat $sysfs_path/${channel}_scale >> $meta_file
	fi
	if [[ -e $sysfs_path/${channel}_offset ]]; then
		echo -n "offset = " >> $meta_file
		cat $sysfs_path/${channel}_offset >> $meta_file
	fi

	echo 1 > $sysfs_path/scan_elements/${channel}_en
}

function process_sensor () {
	local sensor=$1
	local sysfs_path=/sys/bus/iio/devices/$sensor
	local meta_file=$(echo -n "${sensor}" | sed 's/[<>:"\\|?*]/_/g').ini
	local channels
	echo "Processing sensor ${sensor#iio:device}: ${sensor}"
	# Disable buffer to allow enabling scan elements
	echo 0 > $sysfs_path/buffer/enable

	for channel in $sysfs_path/*_raw; do
		channel=$(basename ${channel%_raw})
		if [[ -e $sysfs_path/scan_elements/${channel}_index ]] &&
		   [[ -e $sysfs_path/scan_elements/${channel}_type ]]; then
			channels=${channels}${channels:+" "}$channel
		else
			echo "Channel $channel not in scan_elements. Ignoring"
		fi
	done
	# Add timestamp channel if it exists
	if [[ -e $sysfs_path/scan_elements/in_timestamp_index ]]; then
		channels=${channels}${channels:+" "}in_timestamp
	fi
	echo "Channels: $channels"
	echo -n "" > $meta_file || {
		echo "Failed to write sensor channels metadata file ${meta_file}. Aborting."
		exit 1
	}
	for channel in $channels; do
		setup_channel $sensor $channel
	done
	if [[ -e $sysfs_path/trigger/current_trigger ]]; then
		echo $TRIGGER_NAME > $sysfs_path/trigger/current_trigger
	fi
	echo monotonic > $sysfs_path/current_timestamp_clock
	echo 1 > $sysfs_path/buffer/enable
}

function stop_sensors () {
	kill $running
	running=
	for sensor in $sensors; do
		echo 0 > /sys/bus/iio/devices/$sensor/buffer/enable
	done
}

function handle_sigint () {
	echo "Stopping sensors..."
	stop_sensors
	exit 0
}

if test $# -gt 2 || test $# -lt 1; then
	echo "Usage: $0 SAMPLE_FREQ [CAPTURE_DURATION]"
	exit 1
fi

if test $# -eq 1; then
	capture_duration=0
else
	capture_duration=$2
fi

sample_freq=$1
sensors=

if ! [[ -d /sys/bus/iio/devices ]]; then
	echo "No IIO support detected. Please load IIO modules."
	exit 1
fi

if ! [[ -d /dev/cfg/iio/triggers/hrtimer ]]; then
	echo "No hrtimer trigger support detected, or configfs is not mounted on /dev/cfg"
	exit 1
fi

if ! [[ -d $TRIGGER_DIR ]]; then
	mkdir "$TRIGGER_DIR" || { echo "Failed to create trigger"; exit 1; }
fi

for sensor in /sys/bus/iio/devices/iio:*; do
	sensors=${sensors}${sensors:+" "}$(basename $sensor)
done
running=
for sensor in $sensors; do
	process_sensor $sensor
	cat /dev/${sensor} > $(echo -n "${sensor}" | sed 's/[<>:"\\|?*]/_/g').dat &
	running=${running}${running:+" "}$!
done
trap 'handle_sigint' SIGINT
if [[ $capture_duration -eq 0 ]]; then
	echo "Press the RETURN key to terminate data collection"
	read
else
	sleep $capture_duration
fi
stop_sensors
exit 0
