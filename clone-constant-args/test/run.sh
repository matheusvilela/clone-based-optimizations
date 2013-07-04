#!/bin/bash

BIN=$1
PASS=$2
LIB=$3
OUT='output'

TIME='/usr/bin/time -f%U'
rm -rf $OUT
mkdir $OUT

for BENCH in *.c
do
  BASE=`basename $BENCH .c`
  echo $BASE

  BENCH_BASE=$OUT/$BASE.baseline
  BENCH_PASS=$OUT/$BASE.$PASS

  # generate bytecode
  $BIN/clang -w -emit-llvm -S -c $BENCH -o $BENCH_BASE.bc
  $BIN/opt -load $LIB -$PASS $BENCH_BASE.bc -S > $BENCH_PASS.bc
  
  # output diff size
  DIFF_SIZE=`diff $BENCH_BASE.bc $BENCH_PASS.bc | wc -l`
  echo "diff: "$(($DIFF_SIZE - 4)) # header diff has 4 lines


  # create binaries
  $BIN/clang $BENCH_BASE.bc -o $BENCH_BASE
  $BIN/clang $BENCH_PASS.bc -o $BENCH_PASS

  # run original and transformed
  $TIME ./$BENCH_BASE > /dev/null 2> $BENCH_BASE.out
  $TIME ./$BENCH_PASS > /dev/null 2> $BENCH_PASS.out

  echo "base: "`cat $BENCH_BASE.out`
  echo "pass: "`cat $BENCH_PASS.out`
  echo
done
