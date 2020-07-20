SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_MULTI_HOST_IDM" ] && skip;

. lib/idm_setup

vgchange --lock-start

vgdisplay

i=0
for d in "${BLKS[@]}"; do
        i=$((i+1))
	not lvchange -a ey TESTVG$i/foo
done

sleep 60

i=0
for d in "${BLKS[@]}"; do
        i=$((i+1))
	lvchange -a ey TESTVG$i/foo
	lvchange -a n TESTVG$i/foo
done

i=0
for d in "${BLKS[@]}"; do
        i=$((i+1))
	vgremove -f TESTVG$i
done
