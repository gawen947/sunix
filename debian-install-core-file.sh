#!/bin/sh

if [ -f "$1/$2.real" ]; then
  echo "Core \"$2\" already installed..."
else
  echo "Install \"$2\"..."
  dpkg-divert --add --rename --divert "$1/$2.real" "$1/$2"
  install "$2" "$1"
fi
