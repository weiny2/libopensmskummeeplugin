#!/bin/sh

if [ "$1" == "" ]; then
   echo "$0 <dir>"
   echo "   Build plugin against a master management build installed in <dir>"
   echo "   Remove \"Makfile\" to force rebuild" 
   exit 1
fi

if [ ! -f Makefile ]; then
   prefix=$1
   libdir=$prefix/lib

   if [ ! -d $libdir/mysql ]; then
      ln -s /usr/lib64/mysql $libdir/mysql
   fi

   ./autogen.sh
   ./configure --prefix=$prefix --libdir=$libdir --enable-genders
fi

make install
exit $?

