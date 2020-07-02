#!/usr/bin/env bash

# Copyright (C) 2020 Seagate, Inc. All rights reserved.
#
# This copyrighted material is made available to anyone wishing to use,
# modify, copy, or redistribute it subject to the terms and conditions
# of the GNU General Public License v.2.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA


SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_FAILURE_INJECTION" ] && skip;

BLK1=/dev/sdk2
BLK2=/dev/sdm2

DISK1="${BLK1%?}"
DISK2="${BLK2%?}"

# Cleanup devices
dd if=/dev/zero of="$BLK1" bs=1MB count=1000
wipefs -a "$BLK1" 2>/dev/null || true
dd if=/dev/zero of="$BLK2" bs=1MB count=1000
wipefs -a "$BLK2" 2>/dev/null || true

vgcreate --shared --locktype idm TESTVG1 $BLK1 $BLK2

# Emulate fabric failure
echo 1 > /sys/block/$DISK1/device/delete

# Fail to create new logic volume
not lvcreate -a n --zero n -l 1 -n foo TESTVG1

# Rescan drives so can access sdk again
echo "- - -" > /sys/class/scsi_host/host0/scan

# Create new logic volume
lvcreate -a n --zero n -l 1 -n foo TESTVG1

# Activate logic volume
lvchange TESTVG1/foo -a y

# Emulate fabric failure
echo 1 > /sys/block/$DISK1/device/delete

# Wait for lock time out caused by drive failure
sleep 70

check grep_lvmlockd_dump "S lvm_TESTVG1 kill_vg"
lvmlockctl --drop TESTVG1

# Rescan drives so can access sdk again
echo "- - -" > /sys/class/scsi_host/host0/scan

vgremove -g TESTVG1

pvremove /dev/$BLK1
pvremove /dev/$BLK2
