#!/bin/bash
# Does what the child learned in some games carry to games it has never seen?
# 1. the 13 development games, fresh, each keeping its mind in a scratch folder
# 2. the families those games kept, by name
# 3. the 12 held-back games, fresh (no mind of their own), without and with those families
cd "$(dirname "$0")/.."
T=arc/transfer_mind; rm -rf $T; mkdir -p $T
unset CIALL_LIVE
python arc/arc_bridge.py ar25 cd82 dc22 --budget 4000 --mind $T > arc/transfer_dev1.txt 2>&1 &
python arc/arc_bridge.py g50t lf52 ls20 --budget 4000 --mind $T > arc/transfer_dev2.txt 2>&1 &
python arc/arc_bridge.py r11l s5i5 sc25 sp80 --budget 4000 --mind $T > arc/transfer_dev3.txt 2>&1 &
python arc/arc_bridge.py tn36 tu93 wa30 --budget 4000 --mind $T > arc/transfer_dev4.txt 2>&1 &
wait
LIB=$(cat $T/*.txt | awk '$1=="family" && $3=="1" {print $2}' | sort -u | paste -sd, -)
echo "families kept by the development games: $LIB"
cat arc/transfer_dev[1-4].txt | grep "levels finished" | awk '{t+=$3} END {print "development levels:", t}'
for seed in 1 2; do
  for mode in without with; do
    export CIALL_SEED=$seed
    if [ $mode = with ]; then export CIALL_LIVE="$LIB"; else export CIALL_LIVE=""; fi
    python arc/arc_bridge.py bp35 cn04 ft09 --budget 4000 > arc/transfer_${mode}_${seed}_1.txt 2>&1 &
    python arc/arc_bridge.py ka59 lp85 m0r0 --budget 4000 > arc/transfer_${mode}_${seed}_2.txt 2>&1 &
    python arc/arc_bridge.py re86 sb26 sk48 --budget 4000 > arc/transfer_${mode}_${seed}_3.txt 2>&1 &
    python arc/arc_bridge.py su15 tr87 vc33 --budget 4000 > arc/transfer_${mode}_${seed}_4.txt 2>&1 &
    wait
    cat arc/transfer_${mode}_${seed}_[1-4].txt | grep "levels finished" | awk -v m=$mode -v s=$seed '{t+=$3} END {print "held-back, seed", s, m, "families:", t}'
  done
done
