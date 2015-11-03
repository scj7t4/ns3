#!/bin/sh

cd datasets

for dir in ./*/
do
    dir=${dir%*/}
	echo $dir
	tar -zcv $dir | ssh 192.168.1.128 "cat > /mnt/backup/Programming/rpc_datasets/${dir##*/}.tar.gz"
done
