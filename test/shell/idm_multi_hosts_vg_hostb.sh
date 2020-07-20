SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_MULTI_HOST_IDM" ] && skip;

. lib/idm_setup

vgchange --lock-start

vgdisplay

i=0
for d in "${BLKSALL[@]}"; do
        i=$((i+1))
	vgchange -a ey TESTVG$i
	vgchange -a n TESTVG$i
	vgremove -f TESTVG$i
done
