#!/bin/bash

redis-cli SCRIPT FLUSH
rm ../lua_scripts.h
for i in *.lua; do
	echo -n "#define" $(head -1 $i |cut -d' ' -f2) "" >> ../lua_scripts.h
	a=$(redis-cli SCRIPT LOAD "$(cat $i)")
	echo \"$a\" >> ../lua_scripts.h
done
