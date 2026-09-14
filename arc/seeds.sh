#!/bin/bash
# every game, for each seed given: levels finished per seed, and the average
cd "$(dirname "$0")/.."
tag=$1; shift
for seed in "$@"; do
  export CIALL_SEED=$seed
  python arc/arc_bridge.py ar25 cd82 dc22 bp35 --budget 4000 > arc/seed_${tag}_${seed}_1.txt 2>&1 &
  python arc/arc_bridge.py g50t lf52 ls20 cn04 ft09 --budget 4000 > arc/seed_${tag}_${seed}_2.txt 2>&1 &
  python arc/arc_bridge.py r11l s5i5 sc25 sp80 ka59 lp85 m0r0 --budget 4000 > arc/seed_${tag}_${seed}_3.txt 2>&1 &
  python arc/arc_bridge.py tn36 tu93 wa30 re86 sb26 sk48 su15 tr87 vc33 --budget 4000 > arc/seed_${tag}_${seed}_4.txt 2>&1 &
  wait
done
for seed in "$@"; do
  cat arc/seed_${tag}_${seed}_[1-4].txt | grep "levels finished" | awk -v s=$seed '{t+=$3} END {print "seed", s, "levels", t}'
done | awk '{print; sum+=$4; n++} END {printf "average %.1f over %d seeds\n", sum/n, n}'
