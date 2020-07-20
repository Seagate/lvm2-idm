SKIP_WITH_LVMPOLLD=1

. lib/inittest

[ -z "$LVM_TEST_LOCK_TYPE_IDM" ] && skip;
[ -z "$LVM_TEST_MULTI_HOST_IDM" ] && skip;

. lib/idm_setup
. lib/idm_cleanup

i=0
for d in "${BLKSALL[@]}"; do
	i=$((i+1))
	vgcreate --shared --locktype idm TESTVG$i $d

	for j in {1..20}
	do
		lvcreate -a n --zero n -l 1 -n foo$j TESTVG$i
	done
done

i=0
for d in "${BLKSALL[@]}"; do
	i=$((i+1))

	for j in {1..20}
	do
		lvchange -a ey TESTVG$i/foo$j
	done
done

i=0
for d in "${BLKSALL[@]}"; do
	i=$((i+1))

	for j in {1..20}
	do
		lvchange -a n TESTVG$i/foo$j
	done
done
