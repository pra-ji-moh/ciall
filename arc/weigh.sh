#!/bin/bash
# Weigh one setting of the child against the games: all 25, levels ended.
cd "$(dirname "$0")/.."
tag=$1; off=$2; seed=${3:-1}
export CIALL_OFF="$off"; export CIALL_SEED=$seed; export CIALL_LIVE=""
python arc/arc_bridge.py ar25 cd82 dc22 bp35 cn04 ft09 --budget 3000 > arc/w_${tag}_1.txt 2>&1 &
python arc/arc_bridge.py g50t lf52 ls20 ka59 lp85 m0r0 --budget 3000 > arc/w_${tag}_2.txt 2>&1 &
python arc/arc_bridge.py r11l s5i5 sc25 sp80 re86 sb26 --budget 3000 > arc/w_${tag}_3.txt 2>&1 &
python arc/arc_bridge.py tn36 tu93 wa30 sk48 su15 tr87 vc33 --budget 3000 > arc/w_${tag}_4.txt 2>&1 &
wait
cat arc/w_${tag}_[1-4].txt | grep "levels finished" | awk -v t="$tag" -v o="$off" '{s+=$3} END {printf "%-22s off=[%s] levels %d\n", t, o, s}'
