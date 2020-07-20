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
	for j in {1..20}
	do
		lvchange -a sy TESTVG$i/foo$j
	done
done

i=0
for d in "${BLKSALL[@]}"; do
        i=$((i+1))
	for j in {1..20}
	do
		lvchange -a ey TESTVG$i/foo$j
		lvchange -a n TESTVG$i/foo$j
	done
done

i=0
for d in "${BLKSALL[@]}"; do
        i=$((i+1))
	vgremove -f TESTVG$i
done
