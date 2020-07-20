#!/usr/bin/env bash

export BLKSALL=("/dev/sdb2" "/dev/sdb3" "/dev/sdb4" "/dev/sdb5"
	        "/dev/sdc2" "/dev/sdc3" "/dev/sdc4" "/dev/sdc5"
	        "/dev/sdd2" "/dev/sdd3" "/dev/sdd4" "/dev/sdd5")

export BLKS=("/dev/sdb2" "/dev/sdb3" "/dev/sdb4" "/dev/sdb5")

export BLK1=/dev/sdb2
export BLK2=/dev/sdc2
export BLK3=/dev/sdd2

aux extend_filter_LVMTEST "a|/dev/sdb*|" "a|/dev/sdc*|" "a|/dev/sdd*|"
aux lvmconf "devices/allow_changes_with_duplicate_pvs = 1"
