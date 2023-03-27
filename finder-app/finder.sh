#!/bin/bash

filesdir=$1
searchstr=$2
FILENUM=0
LINEMATCH=0

if [ $# -lt 2 ] || [ $# -gt 2 ]
then
        echo "Arguments MISMATCH USAGE is ./finder.sh <DIRECTORY> <SEARCH STRING>"
        exit 1
fi

if [ ! -d $filesdir ]
then
        echo "Directory ${filesdir} does not exist"
        echo "USAGE is ./finder.sh <FILE PATH> <SEARCH STRING>"
        exit 1
fi


for subfile in "$filesdir"/*
do

        echo "Searching in ${subfile}"
        matchcount=$(grep -c $searchstr $subfile)
        if [ $matchcount -gt 0 ]
        then
                ((FILENUM=FILENUM+1))
        fi
        ((LINEMATCH=LINEMATCH+matchcount))
done

printf "The number of files are %d and the number of matching lines are %d" "${FILENUM}" "${LINEMATCH}"

