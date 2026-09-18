#
_XDCBUILDCOUNT = 
ifneq (,$(findstring path,$(_USEXDCENV_)))
override XDCPATH = /Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/source;/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/kernel/tirtos/packages
override XDCROOT = 
override XDCBUILDCFG = 
endif
ifneq (,$(findstring args,$(_USEXDCENV_)))
override XDCARGS = 
override XDCTARGETS = 
endif
#
ifeq (0,1)
PKGPATH = 
HOSTOS = 
endif
