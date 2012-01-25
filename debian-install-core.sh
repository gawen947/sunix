#!/bin/sh

./debian-install-core-file.sh /usr/bin yes
./debian-install-core-file.sh /bin true
./debian-install-core-file.sh /bin false
./debian-install-core-file.sh /bin echo
./debian-install-core-file.sh /usr/bin basename
./debian-install-core-file.sh /bin sleep
./debian-install-core-file.sh /usr/bin unlink
./debian-install-core-file.sh /bin ln
./debian-install-core-file.sh /bin rm
./debian-install-core-file.sh /bin cp
./debian-install-core-file.sh /bin mv
