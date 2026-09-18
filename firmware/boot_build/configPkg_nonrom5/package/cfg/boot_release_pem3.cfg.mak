# invoke SourceDir generated makefile for boot_release.pem3
boot_release.pem3: .libraries,boot_release.pem3
.libraries,boot_release.pem3: package/cfg/boot_release_pem3.xdl
	$(MAKE) -f package/cfg/boot_release_pem3.src/makefile.libs

clean::
	$(MAKE) -f package/cfg/boot_release_pem3.src/makefile.libs clean

