newmajor:
	$(Q) powershell -file $(realpath ../buildCmds/incVersion.ps1) major "$(realpath user/include/version.h)"
	touch user/user_main.c

newminor:
	$(Q) powershell -file $(realpath ../buildCmds/incVersion.ps1) minor "$(realpath user/include/version.h)"
	touch user/user_main.c

newpatch:
	$(Q) powershell -file $(realpath ../buildCmds/incVersion.ps1) patch "$(realpath user/include/version.h)"
	touch user/user_main.c
	
newbuild:
	$(Q) powershell -file $(realpath ../buildCmds/incVersion.ps1) build "$(realpath user/include/version.h)"
	touch user/user_main.c
