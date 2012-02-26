#!/bin/sh
get_sock() (
  name=$(basename $0 | cut -d'-' -f1)

  case "$name" in
    note)
      echo "$HOME/.gpushn.sock"
      ;;
    todo) 
      echo "$HOME/.gpusht.sock"
      ;;
    *)
      exit 1
      ;;
  esac
)

check_sock() (
  sock=$1
  if [ ! -S $sock ]
  then
    echo "The GPushD socket was not found at \"$sock\"."
    echo "Did you start the gpushd-server?"
    exit 1
  fi
)
