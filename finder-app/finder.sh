#!/bin/sh

filesdir=$1
searchstr=$2

if [ $# -ne 2 ]; then 
    exit 1
fi

if [ ! -e $filesdir ]; then 
    exit 1
fi

n_lines=$(grep -I -r -o $searchstr $filesdir | wc -l)
n_files=$(grep -I -r -l $searchstr $filesdir | wc -l)

echo The number of files are ${n_files} and the number of matching lines are ${n_lines}