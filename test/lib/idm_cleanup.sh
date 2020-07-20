#!/usr/bin/env bash

echo -n "## cleanup devices..."

for d in "${BLKSALL[@]}"; do
	dd if=/dev/zero of="$d" bs=1MB count=1000 || true
	wipefs -a "$d" 2>/dev/null || true
done

i=0
for d in "${BLKSALL[@]}"; do
	i=$((i+1))

	for j in {1..20}
	do
		dmsetup remove /dev/TESTVG$i/foo$j || true
	done

	dmsetup remove /dev/TESTVG$i/foo || true
	dmsetup remove /dev/TESTVG$i || true
done
