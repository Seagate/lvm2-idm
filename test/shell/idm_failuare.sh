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

test -n "$LVM_TEST_LOCK_TYPE_IDM" && initskip

. lib/inittest

aux prepare_vg 3

# Create new logic volume and deactivate it
lvcreate -a n --zero n -l 1 -n foo $vg

# Activate logic volume
lvchange $vg/foo -a y

# Inject failure so cannot send request to drives
idm_inject_failure 100

# Wait for 40s, but the lock will not be time out
sleep 40

# Inject failure with 0% so can access drives
idm_inject_failure 0

# Deactivate logic volume due to locking failure
lvchange $vg/foo -a n

# Activate logic volume
lvchange $vg/foo -a y

# Inject failure so cannot send request to drives
idm_inject_failure 100

# Wait for lock time out caused by drive failure
sleep 70

check grep_lvmlockd_dump "S lvm_$vg kill_vg"
lvmlockctl --drop $vg

# Recovery drive accessing
idm_inject_failure 0

aux teardown_devs
