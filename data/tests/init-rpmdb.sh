rm root -rf
mkdir -p root/var/lib/rpm
mkdir -p root/usr/share
rpm --initdb --dbpath /home/hughsie/Code/zif/data/tests/root/var/lib/rpm
rpm --dbpath  --justdb /home/hughsie/Code/zif/data/tests/root/var/lib/rpm -Uvh ./*.rpm
rpm --dbpath /home/hughsie/Code/zif/data/tests/root/var/lib/rpm -qa

