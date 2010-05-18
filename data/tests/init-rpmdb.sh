rm root -rf
mkdir -p root/var/lib/rpm
mkdir -p root/usr/share
rpm --initdb --dbpath /home/hughsie/Code/zif/data/tests/root/var/lib/rpm
rpm --dbpath /home/hughsie/Code/zif/data/tests/root/var/lib/rpm -Uvh ./test-0.1-1.fc13.noarch.rpm --justdb
rpm --dbpath /home/hughsie/Code/zif/data/tests/root/var/lib/rpm -qa

