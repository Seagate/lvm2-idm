#!/usr/bin/env bash

# Copyright (C) 2008-2012 Red Hat, Inc. All rights reserved.
#
# This copyrighted material is made available to anyone wishing to use,
# modify, copy, or redistribute it subject to the terms and conditions
# of the GNU General Public License v.2.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

test_description='Set up things to run tests with dlm'

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;

aux prepare_idm
aux prepare_lvmlockd

# Clean up drive firmware
sg_raw -v -r 512 -o test_data.bin /dev/sg2  88 00 01 00 00 00 00 20 FF 01 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg2  8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg4  8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg5  8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg7  8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg10 8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg11 8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg12 8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
sg_raw -v -s 512 -i test_data.bin /dev/sg14 8E 00 FF 00 00 00 00 00 00 00 00 00 00 01 00 00
