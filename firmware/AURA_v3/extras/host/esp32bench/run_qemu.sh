#!/bin/sh
# run_qemu.sh <build_dir> <log> : merge the bench image and run it in Espressif QEMU
# (-icount shift=0: 1 instruction = 1 ns of virtual time, so bench "us" = thousands of instructions).
B=$1; LOG=$2
ET=$(ls -d ~/.arduino15/packages/esp32/tools/esptool_py/*/ | tail -1)
$ET/esptool --chip esp32 merge-bin --fill-flash-size 4MB -o $B/flash4m.bin 0x1000 $B/AURA_bench.ino.bootloader.bin \
  0x8000 $B/AURA_bench.ino.partitions.bin 0x10000 $B/AURA_bench.ino.bin >/dev/null 2>&1
/workspace/tools/qemu/bin/qemu-system-xtensa -nographic -machine esp32 -m 4M -icount shift=0 \
  -drive file=$B/flash4m.bin,if=mtd,format=raw -no-reboot > $LOG 2>&1 &
QP=$!
T=0
while [ $T -lt ${QEMU_TIMEOUT:-1500} ]; do
  sleep 5; T=$((T+5))
  if grep -q "\[bench\] done" $LOG; then break; fi
done
kill $QP 2>/dev/null
sed -n '/\[bench\] start/,/\[bench\] done/p' $LOG
