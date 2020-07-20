SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_MULTI_HOST_IDM" ] && skip;

. lib/idm_setup
. lib/idm_cleanup

i=0
for d in "${BLKS[@]}"; do
	i=$((i+1))
	vgcreate --shared --locktype idm TESTVG$i $d
	lvcreate -a n --zero n -l 1 -n foo TESTVG$i
	lvchange -a ey TESTVG$i/foo
done
