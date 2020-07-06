SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_MULTI_HOST_IDM" ] && skip;

aux extend_filter_LVMTEST "a|/dev/sdb*|" "a|/dev/sdc*|" "a|/dev/sdd*|"

BLKS=("/dev/sdb2" "/dev/sdb3" "/dev/sdb4" "/dev/sdb5"
      "/dev/sdc2" "/dev/sdc3" "/dev/sdc4" "/dev/sdc5"
      "/dev/sdd2" "/dev/sdd3" "/dev/sdd4" "/dev/sdd5")

vgchange --lock-start


vgdisplay

i=0
for d in "${BLKS[@]}"; do
        i=$((i+1))
	vgchange -a ey TESTVG$i
	vgchange -a n TESTVG$i
	vgremove -f TESTVG$i
done
