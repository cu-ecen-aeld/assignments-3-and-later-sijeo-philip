#!/bin/bash


writefile=$1
writestr=$2


if [ $# -lt 2 ] || [ $# -gt 2 ]
then
	echo "Mismatch. USAGE ./writer.sh <FILEPATH> <STRING>"
	exit 1
fi
directory=$(dirname "$writefile")
filename=$(basename "$writefile")

if [ ! -d $directory ]
then
	mkdir -p "$directory"
fi

if [ -f $writefile ]
then
	echo "$writestr" > $writefile
else
	touch $writefile	
	echo "$writestr" > $writefile
fi
