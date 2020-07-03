SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_MULTI_HOST_IDM" ] && skip;

aux extend_filter_LVMTEST "a|/dev/sdb*|" "a|/dev/sdc*|" "a|/dev/sdd*|"
aux lvmconf "devices/allow_changes_with_duplicate_pvs = 1"

BLKS=("/dev/sdb2" "/dev/sdb3" "/dev/sdb4" "/dev/sdb5"
      "/dev/sdc2" "/dev/sdc3" "/dev/sdc4" "/dev/sdc5"
      "/dev/sdd2" "/dev/sdd3" "/dev/sdd4" "/dev/sdd5")

for d in "${BLKS[@]}"; do
	dd if=/dev/zero of="$d" bs=1MB count=1000
	wipefs -a "$d" 2>/dev/null || true
done

i=0
for d in "${BLKS[@]}"; do
	i=$((i+1))
	dmsetup remove /dev/TESTVG$i/foo || true
	dmsetup remove /dev/TESTVG$i || true
done

i=0
for d in "${BLKS[@]}"; do
	i=$((i+1))
	vgcreate --shared --locktype idm TESTVG$i $d
	lvcreate -a n --zero n -l 1 -n foo TESTVG$i
	lvchange -a sy TESTVG$i/foo
done
