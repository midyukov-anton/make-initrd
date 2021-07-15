ifeq "$(call if-active-feature,sshfsroot)/$(call if-guess-passed,net)" "sshfsroot/net"
GUESS_NET_IFACE = all
GUESS_MODULES += net
endif
