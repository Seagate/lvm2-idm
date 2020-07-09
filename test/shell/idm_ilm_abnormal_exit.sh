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

aux prepare_vg 3

# Create new logic volume and deactivate it
lvcreate -a n --zero n -l 1 -n foo $vg

# Activate logic volume
lvchange $vg/foo -a y

# Kill IDM lock manager
killall -9 seagate_ilm

# Wait for time out: 60s timeout + 60s quiescent period
sleep 130

not mkfs.ext4 "$DM_DEV_DIR/$vg/foo"

lvmlockctl --kill $vg
lvmlockctl --drop $vg

systemctl restart seagate_ilm

vgremove -f $vg

aux teardown_devs
