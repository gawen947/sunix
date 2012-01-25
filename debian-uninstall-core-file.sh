#!/bin/sh

if [ -f "$1.real" ]
then
  echo "Uninstalling \"$1\"..."
  dpkg-divert --remove "$1"
  mv "$1.real" "$1"
else
  echo "Not installed \"$1\"..."
fi
