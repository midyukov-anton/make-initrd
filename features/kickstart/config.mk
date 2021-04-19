$(call feature-requires,devmapper modules-crypto-user-api system-glibc)

KICKSTART_PROGS  = sfdisk wipefs lvm mdadm cryptsetup blkid findmnt \
		   mkswap mount mountpoint rsync wget tar unzip cpio env \
		   sha256sum
KICKSTART_PROGS_PATTERNS = mkfs.*
KICKSTART_DATADIR = $(FEATURESDIR)/kickstart/data

KICKSTART_CONFIGS ?=
