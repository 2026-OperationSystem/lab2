#!/bin/bash
clean_make(){
   cd xv6 
   make clean
   make kernelmemfs
   cd ..
}
cpy_ker(){
   cp xv6/kernelmemfs image/kernel
}

clean_make
cpy_ker

./run.sh
